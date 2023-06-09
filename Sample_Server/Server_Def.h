#pragma once   //避免头文件被重复包含
#ifndef SERVER_DEF_H  //条件编译保护
#define SERVER_DEF_H
#include <WinSock2.h>
#include <MSWSock.h>
#include <map>
#include <list>
#include <vector>
#include <string>
#include <algorithm>
#include <mutex>
#include <iostream>
#include <mstcpip.h> //tcp_keepalive
#pragma comment(lib, "WS2_32.lib")   //指定链接WINSOCK库文件

using namespace std;


#define MAX_BUFFER_LEN 8192   //缓冲区最大长度限制(8KB)
#define WORKER_THREADS_PER_PROCESSOR 2				//指出了线程与核心数的关系,这里指 线程数=核心数*2
#define MAX_LISTEN_SOCKET SOMAXCONN					//最大listen队列长度
#define MAX_POST_ACCEPT 10							//最大AcceptEx请求队列长度
#define EXIT_CODE 0								//退出标识
#define DEFAULT_IP "127.0.0.1"						//默认工作IP
#define DEFAULT_PORT 10240							//默认工作端口


#define RELEASE_ARRAY(x) {if(x != nullptr ){delete[] x;x=nullptr;}}			//数组释放宏

#define RELEASE_POINTER(x) {if(x != nullptr ){delete x;x=nullptr;}}			//指针释放宏

#define RELEASE_HANDLE(x) {if(x != nullptr && x!=INVALID_HANDLE_VALUE)\
{ CloseHandle(x);x = NULL;}}	//	句柄释放宏

#define RELEASE_SOCKET(x) {if(x != NULL && x !=INVALID_SOCKET) \{ closesocket(x); x = INVALID_SOCKET; }} //套接字释放宏



enum class IOType   // 枚举：标识网络操作的类型
{
    NONE,	// 用于初始化，无意义
    ACCEPT, // 标志投递的Accept操作
    SEND,	// 标志投递的是发送操作
    RECV,	// 标志投递的是接收操作
};

struct IoContext   //PER_IO_CONTEXT结构体声明
{
    OVERLAPPED m_Overlapped;	//重叠结构体，用来异步I/O模型进行通信的核心
    IOType m_PostType;	      // 标识网络操作的类型(对应上面的枚举)

    SOCKET m_acceptSocket;	// 这个网络操作所使用的Socket
    WSABUF m_wsaBuf;			// WSA类型的缓冲区，用于给重叠操作传参数的
    char m_szBuffer[MAX_BUFFER_LEN];	// 这个是WSABUF里具体存字符的缓冲区
    DWORD m_nTotalBytes;		//数据总的字节数
    DWORD m_nSentBytes;	     //已经发送的字节数，如未发送数据则设置为0

    //构造函数
    IoContext()
    {
        m_PostType = IOType::NONE;
        ZeroMemory(&m_Overlapped, sizeof(OVERLAPPED));
        ZeroMemory(m_szBuffer, MAX_BUFFER_LEN);
        m_acceptSocket = INVALID_SOCKET;
        m_wsaBuf.len = MAX_BUFFER_LEN;
        m_wsaBuf.buf = m_szBuffer;
        m_nTotalBytes = 0;
        m_nSentBytes = 0;
    }

    //析构函数
    ~IoContext()
    {
        if (m_acceptSocket != INVALID_SOCKET)
        {
            m_acceptSocket = INVALID_SOCKET;
        }
    }

    //重置缓冲区内容，当IOContext被重复使用时需要清空上一次的遗留数据。
    void ResetBuffer()
    {
        ZeroMemory(m_szBuffer, MAX_BUFFER_LEN);
        m_wsaBuf.len = MAX_BUFFER_LEN;
        m_wsaBuf.buf = m_szBuffer;
    }
};

struct SocketContext   // PER_SOCKET_CONTEXT结构体声明
{
    SOCKET m_Socket;		  // 每一个客户端连接的Socket
    SOCKADDR_IN m_ClientAddr; // 客户端的地址
    vector<IoContext*> m_arrayIoContext;   // 存放与该 Socket 相关联的 IO 上下文

    //构造函数
    SocketContext()
    {
        m_Socket = INVALID_SOCKET;
        memset(&m_ClientAddr, 0, sizeof(m_ClientAddr));
    }

    //析构函数
    ~SocketContext()
    {
        if (m_Socket != INVALID_SOCKET)
        {
            closesocket(m_Socket); //!
            m_Socket = INVALID_SOCKET;
        }
        // 释放掉所有的IO上下文数据
        for (size_t i = 0; i < m_arrayIoContext.size(); i++)
        {
            delete m_arrayIoContext.at(i);
        }
        m_arrayIoContext.clear();
    }

    //进行套接字操作时，调用此函数返回 IoContext 指针（即用于封装每个 IO 操作的上下文变量）
    IoContext* GetNewIoContext()
    {
        IoContext* p = new IoContext;
        m_arrayIoContext.push_back(p);
        return p;
    }

    // 从数组中移除一个指定的IoContext
    void RemoveContext(IoContext* pContext)
    {
        if (pContext == nullptr)
        {
            return;
        }
        vector<IoContext*>::iterator it;
        it = m_arrayIoContext.begin();
        while (it != m_arrayIoContext.end())
        {
            IoContext* pObj = *it;
            if (pContext == pObj)
            {
                delete pContext;
                pContext = nullptr;
                it = m_arrayIoContext.erase(it);
                break;
            }
            it++;
        }
    }
};
#endif  // 条件编译保护结束