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
	//Ͷ��WSASend���󣬷�������
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
	std::lock_guard<std::mutex> lock(this->m_HandleError_Mutex);	//��ֹ���̱߳����ͻ
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

	const DWORD dwExitCodeVal = EXIT_CODE; // ���峣��

	while (WAIT_OBJECT_0 != WaitForSingleObject(pServer->m_hShutdownEvent, 0)) {

		DWORD dwBytesTransfered = 0;
		SocketContext* pSoContext = nullptr;
		OVERLAPPED* pOverlapped = nullptr;

		BOOL bRet = GetQueuedCompletionStatus(pServer->m_hIOCompletionPort,
			&dwBytesTransfered, reinterpret_cast<PULONG_PTR>(&pSoContext), &pOverlapped, INFINITE);


		// ������
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

		if (dwExitCodeVal == DWORD(pSoContext)) // ��鴦���߳��˳��¼�
		{
			break;
		}

		IoContext* pIoContext = CONTAINING_RECORD(pOverlapped, IoContext, m_Overlapped); // ��ȡ����Ĳ���


		// ��鴫����ֽ����Ƿ�Ϊ 0 
		if (dwBytesTransfered == 0
			&& pIoContext && pSoContext
			&& (pIoContext->m_PostType == IOType::RECV || pIoContext->m_PostType == IOType::SEND))
		{
			pServer->OnConnectionClosed(pSoContext);
			pServer->DoClose(pSoContext);

			continue;
		}

		// �� I/O ����ת������Ӧ�ķ���
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

	// �ͷ��ڴ�
	RELEASE_POINTER(pParam);
	return 0;
}


bool TCP_Server::PostAccept(IoContext* pIoContext)
{
	// У������Ƿ�Ϊ nullptr
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
	// У������Ƿ�Ϊ nullptr
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
	// У������Ƿ�Ϊ nullptr
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
	// У������Ƿ�Ϊ nullptr
	if (pSoContext == nullptr || pIoContext == nullptr) {
		std::cerr << "DoRecv: Invalid parameter." << std::endl;
		return false;
	}

	// ���� OnRecvCompleted ������֪ͨӦ�ò�
	this->OnRecvCompleted(pSoContext, pIoContext);

	// ������������
	return true;
}

bool TCP_Server::PostSend(SocketContext* pSoContext, IoContext* pIoContext)
{
	// У������Ƿ�Ϊ nullptr
	if (pSoContext == nullptr || pIoContext == nullptr) {
		std::cerr << "PostSend: Invalid parameter." << std::endl;
		return false;
	}

	// ��ʼ������
	pIoContext->m_PostType = IOType::SEND;
	pIoContext->m_nTotalBytes = 0;
	pIoContext->m_nSentBytes = 0;

	// Ͷ���첽��������
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
	// �˴���Ҫ���Ӳ���У�飬��֤�����ѷ��䲢��Ч
	if (pIoContext == nullptr || pIoContext->m_szBuffer == nullptr) {
		std::cerr << "DoSend: Invalid parameter." << std::endl;
		return false;
	}

	// ���з�������
	if (pIoContext->m_nSentBytes < pIoContext->m_nTotalBytes) {
		//�������δ�����꣬������������
		pIoContext->m_wsaBuf.buf = pIoContext->m_szBuffer + pIoContext->m_nSentBytes;
		pIoContext->m_wsaBuf.len = pIoContext->m_nTotalBytes - pIoContext->m_nSentBytes;
		// ��������
		bool bPostSendOK = this->PostSend(pSoContext, pIoContext);
		if (!bPostSendOK) {
			std::cerr << "DoSend: Failed to post send request." << std::endl;
			return false;
		}
	}
	else {
		// ����������ݶ��Ѿ�������ϣ���֪ͨӦ�ò㲢���� true
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
	// ����У��
	if (pSoContext == nullptr) {
		std::cerr << "AssociateWithIOCP: Invalid parameter." << std::endl;
		return false;
	}

	HANDLE hTemp = CreateIoCompletionPort((HANDLE)pSoContext->m_Socket,
		m_hIOCompletionPort, (DWORD)pSoContext, 0);
	if (nullptr == hTemp) {
		// ������ִ�����Ҫ��ʱ������󲢷��� false
		int nErr = GetLastError();
		std::cerr << "AssociateWithIOCP: Failed to associate with IOCP with error code " << nErr << std::endl;
		this->DoClose(pSoContext);
		return false;
	}

	// ���ӳɹ����򷵻� true
	return true;
}
void TCP_Server::AddToClientsContextsList(SocketContext* pSoContext)
{
	// ���֮ǰ�Ƚ��в���У��
	if (pSoContext == nullptr) {
		std::cerr << "AddToClientsContextsList: Invalid parameter." << std::endl;
		return;
	}

	std::lock_guard<std::mutex> lock(this->m_ClientsContexts_Mutex);

	this->m_ClientsContexts.push_back(pSoContext);
}
void TCP_Server::RemoveClientsFormContextsList(SocketContext* pSoContext)
{
	// ɾ��ǰ�жϿ�ָ������Ҫɾ����SocketContextʵ���Ƿ����б���
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
	// ��ȡϵͳ��Ϣ
	SYSTEM_INFO si;
	GetSystemInfo(&si);

	// �����ȡʧ�ܣ��򷵻� 1��Ĭ��Ϊ�� CPU��
	if (si.dwNumberOfProcessors < 1) {
		std::cerr << "GetNumOfProcessors: Failed to retrieve system information." << std::endl;
		return 1;
	}

	// ���� CPU ��
	return si.dwNumberOfProcessors;
}

bool TCP_Server::LoadSocketLib()
{
	// ��ʼ�� Winsock ��
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
	// ж�� Winsock ��
	WSACleanup();
}

void TCP_Server::ClearClientsContextsList()
{
	std::lock_guard<std::mutex> lock(this->m_ClientsContexts_Mutex);
	// ��ÿһ�� SocketContext ʵ���������ͷţ���ʹ�� nullptr ����Ѿ��ͷŹ����ڴ�
	std::for_each(this->m_ClientsContexts.begin(), this->m_ClientsContexts.end(),
		[](SocketContext*& pObj) {
			if (pObj != nullptr) {
				delete pObj;
				pObj = nullptr;
			}
		});
	// ����б�
	this->m_ClientsContexts.clear();
}
void TCP_Server::DeInitialize() {
	// ɾ���ͻ����б�Ļ�����

	// �ر�ϵͳ�˳��¼����
	RELEASE_HANDLE(this->m_hShutdownEvent);
	// �ͷŹ������߳̾��ָ��
	for (int i = 0; i < this->m_nThreads; i++)
	{
		RELEASE_HANDLE(this->m_phWorkerThreads[i]);
	}

	RELEASE_ARRAY(this->m_phWorkerThreads);
	// �ر�IOCP���
	RELEASE_HANDLE(this->m_hIOCompletionPort);
	// �رռ���Socket
	RELEASE_POINTER(this->m_pListenContext);

}
void TCP_Server::Stop()
{
	// ��� Listen Socket �Ƿ�������
	if (this->m_pListenContext != nullptr && this->m_pListenContext->m_Socket != INVALID_SOCKET) {
		// �������̷߳����˳�֪ͨ
		SetEvent(this->m_hShutdownEvent);
		for (int i = 0; i < this->m_nThreads; i++)
		{
			PostQueuedCompletionStatus(this->m_hIOCompletionPort,
				0, (DWORD)EXIT_CODE, NULL);
		}

		// �ȴ��߳̽���
		WaitForMultipleObjects(this->m_nThreads, this->m_phWorkerThreads,
			TRUE, INFINITE);

		// ��տͻ����б�
		this->ClearClientsContextsList();

		// ����������Դ
		this->DeInitialize();
	}
	else
	{
		// ��� Listen Socket �ѹرգ���ֱ��������ָ�뼴�ɡ�
		this->m_pListenContext = nullptr;
	}
	return;
}

bool TCP_Server::InitializeIOCP()
{
	// ������ɶ˿�
	this->m_hIOCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if (nullptr == this->m_hIOCompletionPort) {
		return false;
	}

	// ���ݱ����еĴ�����������������Ӧ���߳���
	this->m_nThreads = WORKER_THREADS_PER_PROCESSOR * this->GetNumOfProcessors();
	this->m_phWorkerThreads = new HANDLE[this->m_nThreads];

	// ���ݼ�����������������������߳�
	for (int i = 0; i < this->m_nThreads; i++) {

		// ��ʼ���̲߳���
		auto pThreadParams = new WorkerThreadParam;
		pThreadParams->pServer = this;
		pThreadParams->nThreadNo = i + 1;

		// ʹ�� _beginthreadex �ͷ��صľ����ʼ���������߳�
		unsigned int nThreadID = 0;
		this->m_phWorkerThreads[i] = (HANDLE)_beginthreadex(nullptr, 0, (_beginthreadex_proc_type)WorkerThread,
			(void*)pThreadParams, 0, &nThreadID);

		// ����޷��ɹ������������̣߳����ͷ���ǰ������ڴ沢���� false
		if (this->m_phWorkerThreads[i] == nullptr) {
			std::cerr << "InitializeIOCP: Failed to create worker thread with index " << i << "." << std::endl;
			delete pThreadParams;
			this->m_nThreads = i - 1;
			return false;
		}

		// ���߳� ID ��������ṹ����
		pThreadParams->nThreadId = nThreadID;
	}

	return true;
}
bool TCP_Server::InitializeListenSocket()
{
	this->m_pListenContext = new SocketContext;

	// ���� Listen Socket
	this->m_pListenContext->m_Socket = WSASocketA(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == this->m_pListenContext->m_Socket) {
		std::cerr << "InitializeListenSocket: Failed to create Listen Socket with error code " << WSAGetLastError() << "." << std::endl;
		return false;
	}

	// �� Listen Socket ������ɶ˿�
	if (NULL == CreateIoCompletionPort((HANDLE)m_pListenContext->m_Socket, this->m_hIOCompletionPort, (DWORD)m_pListenContext, 0)) {
		std::cerr << "InitializeListenSocket: Failed to bind Listen Socket to I/O Completion Port with error code " << GetLastError() << "." << std::endl;
		return false;
	}

	// ����������ַ��Ϣ
	sockaddr_in serverAddress;
	ZeroMemory((char*)&serverAddress, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddress.sin_port = htons(this->m_nPort);

	// �󶨵�ַ�Ͷ˿�
	if (SOCKET_ERROR == ::bind(this->m_pListenContext->m_Socket, (sockaddr*)&serverAddress, sizeof(serverAddress))) {
		std::cerr << "InitializeListenSocket: Failed to bind Listen Socket to local address and port with error code " << WSAGetLastError() << "." << std::endl;
		return false;
	}

	// ��ʼ���м���
	if (SOCKET_ERROR == ::listen(this->m_pListenContext->m_Socket, MAX_LISTEN_SOCKET)) {
		std::cerr << "InitializeListenSocket: Failed to configure Listen Socket for listening with error code " << WSAGetLastError() << "." << std::endl;
		return false;
	}

	// ʹ�� AcceptEx ����
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

	// Ϊ�������Ͷ�� AcceptEx I/O ����
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
	// ȷ����������Ҫ�����Ķ˿�
	this->m_nPort = p_Port;

	// ����ϵͳ�˳��¼�֪ͨ��������Ը�����Ҫ���¼����������Ը��õ������书�ܡ�
	this->m_hShutdownEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
	if (this->m_hShutdownEvent == NULL) {
		// ��������¼�ʧ������������벢ֱ�ӷ��� false��
		std::cerr << "Failed to create shutdown event with error code " << GetLastError() << "." << std::endl;
		return false;
	}

	// ��ʼ�� IOCP
	if (!this->InitializeIOCP()) {
		// ��� IOCP ��ʼ��ʧ�ܣ��������ʾ��Ϣ������ false��
		std::cerr << "Failed to initialize I/O Completion Port." << std::endl;
		return false;
	}

	// ��ʼ�� Listen Socket
	if (!this->InitializeListenSocket()) {
		// ��� Listen Socket ��ʼ��ʧ�ܣ�������� IOCP �����Դ���ر�ϵͳ�˳��¼��������� false��
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
