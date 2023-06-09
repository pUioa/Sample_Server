#pragma once
#include "Server_Def.h"
class TCP_Server;			//方便下文获取类指针实现类外Call
struct WorkerThreadParam
{
	TCP_Server* pServer;		//类指针，用于调用类中的函数

	int nThreadNo;				//线程编号
	int nThreadId;				//线程ID
	HANDLE hThread;				//线程句柄
};

class TCP_Server {
public:
    // 服务器端连接接受时调用的虚拟回调函数
    virtual void OnConnectionAccepted(SocketContext* pSoContext) {};
    // 服务器端连接关闭时调用的虚拟回调函数
    virtual void OnConnectionClosed(SocketContext* pSoContext) {};
    // 在服务器端建立连接期间发生错误时调用的虚拟回调函数
    virtual void OnConnectionError(SocketContext* pSoContext, int error) {};
    // 当数据接收操作完成时调用的虚拟回调函数
    virtual void OnRecvCompleted(SocketContext* pSoContext, IoContext* pIoContext) {};
    // 当数据发送操作完成时调用的虚拟回调函数
    virtual void OnSendCompleted(SocketContext* pSoContext, IoContext* pIoContext) {};

    // 发送数据到指定套接字
    bool SendData(SocketContext* pSoContext, char* data, int size);
    // 发送缓冲区中的所有数据到指定套接字
    bool SendData(SocketContext* pSoContext, IoContext* pIoContext);
    // 接收数据从指定套接字
    bool RecvData(SocketContext* pSoContext, IoContext* pIoContext);
    // 返回当前套接字出错计数
    DWORD GetErrorCount();
    // 返回当前投递的Accept数量
    DWORD GetAcceptPostCount();
    // 返回当前连接数量
    DWORD GetConnectCount();
    // 返回服务器监听端口号
    DWORD GetPort();

private:
    // 服务器安全关闭事件句柄
    HANDLE m_hShutdownEvent;
    // 完成端口句柄
    HANDLE m_hIOCompletionPort;
    // 工作者线程句柄指针
    HANDLE* m_phWorkerThreads;
    // 生成的工作者线程数量
    DWORD m_nThreads;
    // 服务器地址IP
    std::string m_strIP;
    // 服务器监听端口号
    DWORD m_nPort;
    // 连接表互斥量
    std::mutex m_ClientsContexts_Mutex;
    // 错误互斥量
    std::mutex m_HandleError_Mutex;
    // 连接表
    std::list<SocketContext*> m_ClientsContexts;
    // 监听者套接字上下文信息
    SocketContext* m_pListenContext;
    // 当前投递的Accept数量
    DWORD m_AcceptPostCount;
    // 当前连接数量
    DWORD m_ConnectCount;
    // 当前错误数量
    DWORD m_ErrorCount;
    // 接受与AcceptEx有关的socket地址信息函数指针
    LPFN_GETACCEPTEXSOCKADDRS m_lpfnGetAcceptExSockAddrs;
    // AcceptEx函数指针
    LPFN_ACCEPTEX m_lpfnAcceptEx;

    // 获取CPU核心数量
    DWORD GetNumOfProcessors();
    // 确定套接字是否有效
    bool IsSocketAlive(SOCKET s);
    // 处理套接字错误
    bool HandleError(SocketContext* pSoContext, const DWORD& dwErr);
    // 工作者线程处理函数
    static DWORD WINAPI WorkerThread(LPVOID lpParam);
    // 投递AcceptEx操作
    bool PostAccept(IoContext* pIoContext);
    // 执行AcceptEx操作，加入新客户端并开始数据传输
    bool DoAccept(SocketContext* pSoContext, IoContext* pIoContext);
    // 投递Recv操作以侦听数据
    bool PostRecv(SocketContext* pSoContext, IoContext* pIoContext);
    // 从指定套接字接收数据
    bool DoRecv(SocketContext* pSoContext, IoContext* pIoContext);
    // 投递Send操作以在套接字上发送数据
    bool PostSend(SocketContext* pSoContext, IoContext* pIoContext);
    // 将缓冲区中的数据发送到套接字上
    bool DoSend(SocketContext* pSoContext, IoContext* pIoContext);
    // 关闭指定套接字
    bool DoClose(SocketContext* pSoContext);
    // 与完成端口关联
    bool AssociateWithIOCP(SocketContext* pSoContext);
    // 向客户端连接列表中添加新的客户端上下文信息
    void AddToClientsContextsList(SocketContext* pSoContext);
    // 从客户端连接列表中移除已关闭或失效的客户端
    void RemoveClientsFormContextsList(SocketContext* pSoContext);
    // 清空客户端连接列表
    void ClearClientsContextsList();

public:
    // 默认构造函数
    TCP_Server();
    // 析构函数
    ~TCP_Server();
    // 初始化Socket库
    bool LoadSocketLib();
    // 卸载Socket库
    void UnloadSocketLib();
    // 初始化IOCP环境
    bool InitializeIOCP();
    // 初始化监听套接字
    bool InitializeListenSocket();
    // 启动服务器，开始监听连接请求
    bool Start(DWORD p_Port);
    // 停止服务器，并且析构所有资源
    void DeInitialize();
    // 安全停止操作，用于等待线程直到退出
    void Stop();
};
