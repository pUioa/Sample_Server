#include "Server_Base.h"

DWORD TCP_Server::GetErrorCount() {
	return this->m_ErrorCount;
}
DWORD TCP_Server::GetAcceptPostCount() {
	return this->m_AcceptPostCount;
}
DWORD TCP_Server::GetConnectCount() {
	return this->m_ConnectCount;
}
DWORD TCP_Server::GetPort() {
	return this->m_nPort;
}
bool TCP_Server::SendData(SocketContext* pSoContext, char* data, int size)
{
	if (!pSoContext || !data || size <= 0 || size > MAX_BUFFER_LEN)
	{
		return false;
	}
	//投递WSASend请求，发送数据
	IoContext* pNewIoContext = pSoContext->GetNewIoContext();
	pNewIoContext->m_acceptSocket = pSoContext->m_Socket;
	pNewIoContext->m_PostType = IOType::SEND;
	pNewIoContext->m_nTotalBytes = size;
	pNewIoContext->m_wsaBuf.len = size;
	memcpy(pNewIoContext->m_wsaBuf.buf, data, size);
	if (!this->PostSend(pSoContext, pNewIoContext))
	{
		return false;
	}
	return true;
}
bool TCP_Server::SendData(SocketContext* pSoContext, IoContext* pIoContext)
{
	return this->PostSend(pSoContext, pIoContext);
}
bool TCP_Server::RecvData(SocketContext* pSoContext, IoContext* pIoContext)
{
	return this->PostRecv(pSoContext, pIoContext);
}

bool TCP_Server::IsSocketAlive(SOCKET s)
{
	const int nByteSent = ::send(s, "", 0, 0);
	if (SOCKET_ERROR == nByteSent)
	{
		int error = WSAGetLastError();
		std::cerr << "IsSocketAlive failed with error: " << error << std::endl;
		return false;
	}

	return true;
}
bool TCP_Server::HandleError(SocketContext* pSoContext, const DWORD& dwErr)
{
	std::lock_guard<std::mutex> lock(this->m_HandleError_Mutex);	//防止多线程报错冲突
	bool isClosed = false;

	if (WAIT_TIMEOUT == dwErr)
	{
		if (!this->IsSocketAlive(pSoContext->m_Socket))
		{
			isClosed = true;
			std::cerr << "HandleError: Client socket timed out and appears to be disconnected. With Error connection count:"<<this->GetErrorCount()<<"." << std::endl;
		}
	}
	else if (ERROR_NETNAME_DELETED == dwErr)
	{
		isClosed = true;
		std::cerr << "HandleError: Client socket was aborted or has been closed gracefully. With Current connection count:" << this->GetConnectCount() << "." << std::endl;
	}

	if (isClosed)
	{
		this->OnConnectionError(pSoContext, dwErr);
		this->DoClose(pSoContext);
	}
	else
	{
		this->OnConnectionError(pSoContext, dwErr);
		this->DoClose(pSoContext);
		return false;
	}

	return true;
}

DWORD TCP_Server::WorkerThread(LPVOID lpParam) {
	auto pParam = static_cast<WorkerThreadParam*>(lpParam);
	auto pServer = static_cast<TCP_Server*>(pParam->pServer);
	const int nThreadId = pParam->nThreadId;

	const DWORD dwExitCodeVal = EXIT_CODE; // 定义常量

	while (WAIT_OBJECT_0 != WaitForSingleObject(pServer->m_hShutdownEvent, 0)) {

		DWORD dwBytesTransfered = 0;
		SocketContext* pSoContext = nullptr;
		OVERLAPPED* pOverlapped = nullptr;

		BOOL bRet = GetQueuedCompletionStatus(pServer->m_hIOCompletionPort,
			&dwBytesTransfered, reinterpret_cast<PULONG_PTR>(&pSoContext), &pOverlapped, INFINITE);


		// 错误处理
		if (!bRet)
		{
			const DWORD dwErr = GetLastError();
			if (WaitForSingleObject(pServer->m_hShutdownEvent, 0) == WAIT_OBJECT_0)
			{
				break;
			}

			if (!pServer->HandleError(pSoContext, dwErr))
				break;


			continue;
		}

		if (dwExitCodeVal == DWORD(pSoContext)) // 检查处理线程退出事件
		{
			break;
		}

		IoContext* pIoContext = CONTAINING_RECORD(pOverlapped, IoContext, m_Overlapped); // 读取传入的参数


		// 检查传输的字节数是否为 0 
		if (dwBytesTransfered == 0
			&& pIoContext && pSoContext
			&& (pIoContext->m_PostType == IOType::RECV || pIoContext->m_PostType == IOType::SEND))
		{
			pServer->OnConnectionClosed(pSoContext);
			pServer->DoClose(pSoContext);

			continue;
		}

		// 将 I/O 请求转发到对应的方法
		switch (pIoContext->m_PostType)
		{
		case IOType::ACCEPT:
		{
			pIoContext->m_nTotalBytes = dwBytesTransfered;
			pServer->DoAccept(pSoContext, pIoContext);
			break;
		}

		case IOType::RECV:
		{

			pIoContext->m_nTotalBytes = dwBytesTransfered;
			pServer->DoRecv(pSoContext, pIoContext);
			break;
		}

		case IOType::SEND:
		{
			pIoContext->m_nSentBytes += dwBytesTransfered;
			pServer->DoSend(pSoContext, pIoContext);

			break;
		}

		default:
			std::cerr << "WorkerThread: Invalid I/O type. Thread ID: " << nThreadId << std::endl;
			break;
		}
	}

	// 释放内存
	RELEASE_POINTER(pParam);
	return 0;
}


bool TCP_Server::PostAccept(IoContext* pIoContext)
{
	// 校验参数是否为 nullptr
	if (pIoContext == nullptr) {
		std::cerr << "PostAccept: Invalid parameter." << std::endl;
		return false;
	}

	if (this->m_pListenContext == nullptr || this->m_pListenContext->m_Socket == INVALID_SOCKET)
	{
		cerr << "Error: Listen context is invalid." << endl;
		return false;
	}

	// Prepare the parameter.
	pIoContext->ResetBuffer();
	pIoContext->m_PostType = IOType::ACCEPT;
	pIoContext->m_acceptSocket = WSASocket(AF_INET, SOCK_STREAM,
		IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);

	if (pIoContext->m_acceptSocket == INVALID_SOCKET)
	{
		cerr << "Error: Failed to create a new socket. error code = "
			<< WSAGetLastError() << endl;
		return false;
	}

	DWORD dwBytes = 0;
	DWORD dwAddrLen = (sizeof(SOCKADDR_IN) + 16);
	WSABUF* pWSAbuf = &pIoContext->m_wsaBuf;
	if (!this->m_lpfnAcceptEx(m_pListenContext->m_Socket,
		pIoContext->m_acceptSocket, pWSAbuf->buf,
		0, dwAddrLen, dwAddrLen, &dwBytes, &pIoContext->m_Overlapped))
	{
		int nErr = WSAGetLastError();
		if (nErr != WSA_IO_PENDING)
		{
			cerr << "Error: Failed to post an AcceptEx operation. error code = "
				<< nErr << endl;
			return false;
		}
	}

	InterlockedIncrement(&this->m_AcceptPostCount);
	return true;
}
bool TCP_Server::DoAccept(SocketContext* pSoContext, IoContext* pIoContext)
{
	// 校验参数是否为 nullptr
	if (pSoContext == nullptr || pIoContext == nullptr) {
		std::cerr << "DoAccept: Invalid parameter." << std::endl;
		return false;
	}

	InterlockedIncrement(&this->m_ConnectCount);
	InterlockedIncrement(&this->m_AcceptPostCount);

	SOCKADDR_IN* clientAddr = NULL, * localAddr = NULL;
	DWORD dwAddrLen = (sizeof(SOCKADDR_IN) + 16);
	int remoteLen = 0, localLen = 0;
	this->m_lpfnGetAcceptExSockAddrs(pIoContext->m_wsaBuf.buf,
		0,
		dwAddrLen, dwAddrLen, (LPSOCKADDR*)&localAddr,
		&localLen, (LPSOCKADDR*)&clientAddr, &remoteLen);

	SocketContext* pNewSocketContext = new SocketContext;
	this->AddToClientsContextsList(pNewSocketContext);
	pNewSocketContext->m_Socket = pIoContext->m_acceptSocket;
	memcpy(&(pNewSocketContext->m_ClientAddr),
		clientAddr, sizeof(SOCKADDR_IN));

	if (!this->PostAccept(pIoContext))
	{
		pSoContext->RemoveContext(pIoContext);
	}

	if (!this->AssociateWithIOCP(pNewSocketContext))
	{
		cerr << "Failed to associate with IOCP: error code " << GetLastError() << endl;
		delete pNewSocketContext;
		return false;
	}

	tcp_keepalive alive_in = { 0 }, alive_out = { 0 };
	alive_in.keepalivetime = 1000 * 60;
	alive_in.keepaliveinterval = 1000 * 10;
	alive_in.onoff = TRUE;
	DWORD lpcbBytesReturned = 0;
	if (SOCKET_ERROR == WSAIoctl(pNewSocketContext->m_Socket,
		SIO_KEEPALIVE_VALS, &alive_in, sizeof(alive_in), &alive_out,
		sizeof(alive_out), &lpcbBytesReturned, NULL, NULL))
	{
		cerr << "Failed to set tcp_keepalive: error code " << WSAGetLastError() << endl;
	}
	this->OnConnectionAccepted(pNewSocketContext);

	IoContext* pNewIoContext = pNewSocketContext->GetNewIoContext();
	if (pNewIoContext != NULL)
	{
		pNewIoContext->m_PostType = IOType::RECV;
		pNewIoContext->m_acceptSocket = pNewSocketContext->m_Socket;
		return this->PostRecv(pNewSocketContext, pNewIoContext);
	}
	else
	{
		cerr << "Failed to get new IoContext: error code " << GetLastError() << endl;
		this->DoClose(pNewSocketContext);
		return false;
	}
}

bool TCP_Server::PostRecv(SocketContext* pSoContext, IoContext* pIoContext)
{
	// 校验参数是否为 nullptr
	if (pSoContext == nullptr || pIoContext == nullptr) {
		std::cerr << "PostRecv: Invalid parameter." << std::endl;
		return false;
	}

	pIoContext->ResetBuffer();
	pIoContext->m_PostType = IOType::RECV;
	pIoContext->m_nTotalBytes = 0;
	pIoContext->m_nSentBytes = 0;

	DWORD dwFlags = 0, dwBytes = 0;
	int nBytesRecv = WSARecv(pIoContext->m_acceptSocket,
		&pIoContext->m_wsaBuf, 1, &dwBytes, &dwFlags,
		&pIoContext->m_Overlapped, NULL);

	if (nBytesRecv == SOCKET_ERROR) {
		int nErr = WSAGetLastError();
		if (nErr != WSA_IO_PENDING) {
			cerr << "Failed to post WSARecv request: error code " << nErr << endl;
			this->DoClose(pSoContext);
			return false;
		}
	}
	return true;
}
bool TCP_Server::DoRecv(SocketContext* pSoContext, IoContext* pIoContext)
{
	// 校验参数是否为 nullptr
	if (pSoContext == nullptr || pIoContext == nullptr) {
		std::cerr << "DoRecv: Invalid parameter." << std::endl;
		return false;
	}

	// 调用 OnRecvCompleted 方法来通知应用层
	this->OnRecvCompleted(pSoContext, pIoContext);

	// 继续接收数据
	return true;
}

bool TCP_Server::PostSend(SocketContext* pSoContext, IoContext* pIoContext)
{
	// 校验参数是否为 nullptr
	if (pSoContext == nullptr || pIoContext == nullptr) {
		std::cerr << "PostSend: Invalid parameter." << std::endl;
		return false;
	}

	// 初始化变量
	pIoContext->m_PostType = IOType::SEND;
	pIoContext->m_nTotalBytes = 0;
	pIoContext->m_nSentBytes = 0;

	// 投递异步发送请求
	const DWORD dwFlags = 0;
	DWORD dwSendNumBytes = 0;
	const int nRet = WSASend(pIoContext->m_acceptSocket,
		&pIoContext->m_wsaBuf, 1, &dwSendNumBytes, dwFlags,
		&pIoContext->m_Overlapped, NULL);

	if (nRet == SOCKET_ERROR) {
		int nErr = WSAGetLastError();
		if (nErr != WSA_IO_PENDING) {
			std::cerr << "PostSend: Failed to post send request with error code " << nErr << std::endl;
			this->DoClose(pSoContext);
			return false;
		}
	}
	return true;
}
bool TCP_Server::DoSend(SocketContext* pSoContext, IoContext* pIoContext)
{
	// 此处需要增加参数校验，保证缓存已分配并有效
	if (pIoContext == nullptr || pIoContext->m_szBuffer == nullptr) {
		std::cerr << "DoSend: Invalid parameter." << std::endl;
		return false;
	}

	// 进行发送数据
	if (pIoContext->m_nSentBytes < pIoContext->m_nTotalBytes) {
		//如果数据未发送完，继续发送数据
		pIoContext->m_wsaBuf.buf = pIoContext->m_szBuffer + pIoContext->m_nSentBytes;
		pIoContext->m_wsaBuf.len = pIoContext->m_nTotalBytes - pIoContext->m_nSentBytes;
		// 发送请求
		bool bPostSendOK = this->PostSend(pSoContext, pIoContext);
		if (!bPostSendOK) {
			std::cerr << "DoSend: Failed to post send request." << std::endl;
			return false;
		}
	}
	else {
		// 如果所有数据都已经发送完毕，则通知应用层并返回 true
		this->OnSendCompleted(pSoContext, pIoContext);
		return true;
	}

	return true;
}

bool TCP_Server::DoClose(SocketContext* pSoContext)
{
	if (pSoContext != m_pListenContext)
	{
		InterlockedDecrement(&this->m_ConnectCount);
		this->RemoveClientsFormContextsList(pSoContext);
		return true;
	}
	InterlockedIncrement(&this->m_ErrorCount);
	return false;
}

bool TCP_Server::AssociateWithIOCP(SocketContext* pSoContext)
{
	// 参数校验
	if (pSoContext == nullptr) {
		std::cerr << "AssociateWithIOCP: Invalid parameter." << std::endl;
		return false;
	}

	HANDLE hTemp = CreateIoCompletionPort((HANDLE)pSoContext->m_Socket,
		m_hIOCompletionPort, (DWORD)pSoContext, 0);
	if (nullptr == hTemp) {
		// 如果出现错误，需要及时处理错误并返回 false
		int nErr = GetLastError();
		std::cerr << "AssociateWithIOCP: Failed to associate with IOCP with error code " << nErr << std::endl;
		this->DoClose(pSoContext);
		return false;
	}

	// 连接成功，则返回 true
	return true;
}
void TCP_Server::AddToClientsContextsList(SocketContext* pSoContext)
{
	// 添加之前先进行参数校验
	if (pSoContext == nullptr) {
		std::cerr << "AddToClientsContextsList: Invalid parameter." << std::endl;
		return;
	}

	std::lock_guard<std::mutex> lock(this->m_ClientsContexts_Mutex);

	this->m_ClientsContexts.push_back(pSoContext);
}
void TCP_Server::RemoveClientsFormContextsList(SocketContext* pSoContext)
{
	// 删除前判断空指针且需要删除的SocketContext实例是否在列表中
	if (pSoContext == nullptr) {
		std::cerr << "RemoveClientsFormContextsList: Invalid parameter." << std::endl;
		return;
	}

	std::lock_guard<std::mutex> lock(this->m_ClientsContexts_Mutex);

	auto new_end = std::remove_if(this->m_ClientsContexts.begin(), this->m_ClientsContexts.end(),
		[pSoContext](SocketContext* p) { return p == pSoContext; });
	this->m_ClientsContexts.erase(new_end, this->m_ClientsContexts.end());
}

DWORD TCP_Server::GetNumOfProcessors()
{
	// 获取系统信息
	SYSTEM_INFO si;
	GetSystemInfo(&si);

	// 如果获取失败，则返回 1（默认为单 CPU）
	if (si.dwNumberOfProcessors < 1) {
		std::cerr << "GetNumOfProcessors: Failed to retrieve system information." << std::endl;
		return 1;
	}

	// 返回 CPU 数
	return si.dwNumberOfProcessors;
}

bool TCP_Server::LoadSocketLib()
{
	// 初始化 Winsock 库
	WSADATA wsaData;
	int nRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (nRet != 0) {
		std::cerr << "LoadSocketLib: Failed to initialize Winsock with error code " << nRet << "." << std::endl;
		return false;
	}
	return true;
}
void TCP_Server::UnloadSocketLib()
{
	// 卸载 Winsock 库
	WSACleanup();
}

void TCP_Server::ClearClientsContextsList()
{
	std::lock_guard<std::mutex> lock(this->m_ClientsContexts_Mutex);
	// 对每一个 SocketContext 实例都进行释放，并使用 nullptr 标记已经释放过的内存
	std::for_each(this->m_ClientsContexts.begin(), this->m_ClientsContexts.end(),
		[](SocketContext*& pObj) {
			if (pObj != nullptr) {
				delete pObj;
				pObj = nullptr;
			}
		});
	// 清空列表
	this->m_ClientsContexts.clear();
}
void TCP_Server::DeInitialize() {
	// 删除客户端列表的互斥量

	// 关闭系统退出事件句柄
	RELEASE_HANDLE(this->m_hShutdownEvent);
	// 释放工作者线程句柄指针
	for (int i = 0; i < this->m_nThreads; i++)
	{
		RELEASE_HANDLE(this->m_phWorkerThreads[i]);
	}

	RELEASE_ARRAY(this->m_phWorkerThreads);
	// 关闭IOCP句柄
	RELEASE_HANDLE(this->m_hIOCompletionPort);
	// 关闭监听Socket
	RELEASE_POINTER(this->m_pListenContext);

}
void TCP_Server::Stop()
{
	// 检查 Listen Socket 是否已启动
	if (this->m_pListenContext != nullptr && this->m_pListenContext->m_Socket != INVALID_SOCKET) {
		// 向所有线程发出退出通知
		SetEvent(this->m_hShutdownEvent);
		for (int i = 0; i < this->m_nThreads; i++)
		{
			PostQueuedCompletionStatus(this->m_hIOCompletionPort,
				0, (DWORD)EXIT_CODE, NULL);
		}

		// 等待线程结束
		WaitForMultipleObjects(this->m_nThreads, this->m_phWorkerThreads,
			TRUE, INFINITE);

		// 清空客户端列表
		this->ClearClientsContextsList();

		// 回收其他资源
		this->DeInitialize();
	}
	else
	{
		// 如果 Listen Socket 已关闭，则直接清空相关指针即可。
		this->m_pListenContext = nullptr;
	}
	return;
}

bool TCP_Server::InitializeIOCP()
{
	// 创建完成端口
	this->m_hIOCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if (nullptr == this->m_hIOCompletionPort) {
		return false;
	}

	// 根据本机中的处理器数量，建立对应的线程数
	this->m_nThreads = WORKER_THREADS_PER_PROCESSOR * this->GetNumOfProcessors();
	this->m_phWorkerThreads = new HANDLE[this->m_nThreads];

	// 根据计算出来的数量建立工作者线程
	for (int i = 0; i < this->m_nThreads; i++) {

		// 初始化线程参数
		auto pThreadParams = new WorkerThreadParam;
		pThreadParams->pServer = this;
		pThreadParams->nThreadNo = i + 1;

		// 使用 _beginthreadex 和返回的句柄初始化工作者线程
		unsigned int nThreadID = 0;
		this->m_phWorkerThreads[i] = (HANDLE)_beginthreadex(nullptr, 0, (_beginthreadex_proc_type)WorkerThread,
			(void*)pThreadParams, 0, &nThreadID);

		// 如果无法成功启动工作者线程，则释放先前申请的内存并返回 false
		if (this->m_phWorkerThreads[i] == nullptr) {
			std::cerr << "InitializeIOCP: Failed to create worker thread with index " << i << "." << std::endl;
			delete pThreadParams;
			this->m_nThreads = i - 1;
			return false;
		}

		// 将线程 ID 存入参数结构体中
		pThreadParams->nThreadId = nThreadID;
	}

	return true;
}
bool TCP_Server::InitializeListenSocket()
{
	this->m_pListenContext = new SocketContext;

	// 建立 Listen Socket
	this->m_pListenContext->m_Socket = WSASocketA(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == this->m_pListenContext->m_Socket) {
		std::cerr << "InitializeListenSocket: Failed to create Listen Socket with error code " << WSAGetLastError() << "." << std::endl;
		return false;
	}

	// 将 Listen Socket 绑定至完成端口
	if (NULL == CreateIoCompletionPort((HANDLE)m_pListenContext->m_Socket, this->m_hIOCompletionPort, (DWORD)m_pListenContext, 0)) {
		std::cerr << "InitializeListenSocket: Failed to bind Listen Socket to I/O Completion Port with error code " << GetLastError() << "." << std::endl;
		return false;
	}

	// 填充服务器地址信息
	sockaddr_in serverAddress;
	ZeroMemory((char*)&serverAddress, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddress.sin_port = htons(this->m_nPort);

	// 绑定地址和端口
	if (SOCKET_ERROR == ::bind(this->m_pListenContext->m_Socket, (sockaddr*)&serverAddress, sizeof(serverAddress))) {
		std::cerr << "InitializeListenSocket: Failed to bind Listen Socket to local address and port with error code " << WSAGetLastError() << "." << std::endl;
		return false;
	}

	// 开始进行监听
	if (SOCKET_ERROR == ::listen(this->m_pListenContext->m_Socket, MAX_LISTEN_SOCKET)) {
		std::cerr << "InitializeListenSocket: Failed to configure Listen Socket for listening with error code " << WSAGetLastError() << "." << std::endl;
		return false;
	}

	// 使用 AcceptEx 函数
	DWORD dwBytes = 0;
	GUID GuidAcceptEx = WSAID_ACCEPTEX;
	GUID GuidGetAcceptExSockAddrs = WSAID_GETACCEPTEXSOCKADDRS;

	if (SOCKET_ERROR == WSAIoctl(this->m_pListenContext->m_Socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidAcceptEx,
		sizeof(GuidAcceptEx), &this->m_lpfnAcceptEx, sizeof(this->m_lpfnAcceptEx),
		&dwBytes, NULL, NULL))
	{
		std::cerr << "InitializeListenSocket: Failed to get extension function pointer for AcceptEx with error code " << WSAGetLastError() << "." << std::endl;
		return false;
	}

	if (SOCKET_ERROR == WSAIoctl(this->m_pListenContext->m_Socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidGetAcceptExSockAddrs,
		sizeof(GuidGetAcceptExSockAddrs), &this->m_lpfnGetAcceptExSockAddrs,
		sizeof(this->m_lpfnGetAcceptExSockAddrs), &dwBytes, NULL, NULL))
	{
		std::cerr << "InitializeListenSocket: Failed to get extension function pointer for GetAcceptExSockAddrs with error code " << WSAGetLastError() << "." << std::endl;
		return false;
	}

	// 为多个连接投递 AcceptEx I/O 请求
	for (size_t i = 0; i < MAX_POST_ACCEPT; i++)
	{
		IoContext* pIoContext = this->m_pListenContext->GetNewIoContext();
		if (pIoContext == nullptr || !this->PostAccept(pIoContext)) {
			std::cerr << "InitializeListenSocket: Failed to post AcceptEx request with error code " << WSAGetLastError() << "." << std::endl;
			this->m_pListenContext->RemoveContext(pIoContext);
			return false;
		}
	}

	return true;
}
bool TCP_Server::Start(DWORD p_Port)
{
	// 确定服务器需要监听的端口
	this->m_nPort = p_Port;

	// 建立系统退出事件通知，这里可以根据需要对事件进行命名以更好地描述其功能。
	this->m_hShutdownEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
	if (this->m_hShutdownEvent == NULL) {
		// 如果创建事件失败则输出错误码并直接返回 false。
		std::cerr << "Failed to create shutdown event with error code " << GetLastError() << "." << std::endl;
		return false;
	}

	// 初始化 IOCP
	if (!this->InitializeIOCP()) {
		// 如果 IOCP 初始化失败，则输出提示信息并返回 false。
		std::cerr << "Failed to initialize I/O Completion Port." << std::endl;
		return false;
	}

	// 初始化 Listen Socket
	if (!this->InitializeListenSocket()) {
		// 如果 Listen Socket 初始化失败，则需回收 IOCP 相关资源、关闭系统退出事件，并返回 false。
		std::cerr << "Failed to initialize Listen Socket." << std::endl;
		this->DeInitialize();
		return false;
	}
	return true;
}

TCP_Server::TCP_Server() :
	m_nThreads(0),
	m_hShutdownEvent(nullptr),
	m_hIOCompletionPort(nullptr),
	m_phWorkerThreads(nullptr),
	m_strIP(DEFAULT_IP),
	m_nPort(DEFAULT_PORT),
	m_lpfnAcceptEx(nullptr),
	m_lpfnGetAcceptExSockAddrs(nullptr),
	m_pListenContext(nullptr),
	m_AcceptPostCount(0),
	m_ConnectCount(0),
	m_ErrorCount(0)
{
	this->LoadSocketLib();
}
TCP_Server::~TCP_Server() {

}
