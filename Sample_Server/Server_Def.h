#pragma once   //����ͷ�ļ����ظ�����
#ifndef SERVER_DEF_H  //�������뱣��
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
#pragma comment(lib, "WS2_32.lib")   //ָ������WINSOCK���ļ�

using namespace std;


#define MAX_BUFFER_LEN 8192   //��������󳤶�����(8KB)
#define WORKER_THREADS_PER_PROCESSOR 2				//ָ�����߳���������Ĺ�ϵ,����ָ �߳���=������*2
#define MAX_LISTEN_SOCKET SOMAXCONN					//���listen���г���
#define MAX_POST_ACCEPT 10							//���AcceptEx������г���
#define EXIT_CODE 0								//�˳���ʶ
#define DEFAULT_IP "127.0.0.1"						//Ĭ�Ϲ���IP
#define DEFAULT_PORT 10240							//Ĭ�Ϲ����˿�


#define RELEASE_ARRAY(x) {if(x != nullptr ){delete[] x;x=nullptr;}}			//�����ͷź�

#define RELEASE_POINTER(x) {if(x != nullptr ){delete x;x=nullptr;}}			//ָ���ͷź�

#define RELEASE_HANDLE(x) {if(x != nullptr && x!=INVALID_HANDLE_VALUE)\
{ CloseHandle(x);x = NULL;}}	//	����ͷź�

#define RELEASE_SOCKET(x) {if(x != NULL && x !=INVALID_SOCKET) \{ closesocket(x); x = INVALID_SOCKET; }} //�׽����ͷź�



enum class IOType   // ö�٣���ʶ�������������
{
    NONE,	// ���ڳ�ʼ����������
    ACCEPT, // ��־Ͷ�ݵ�Accept����
    SEND,	// ��־Ͷ�ݵ��Ƿ��Ͳ���
    RECV,	// ��־Ͷ�ݵ��ǽ��ղ���
};

struct IoContext   //PER_IO_CONTEXT�ṹ������
{
    OVERLAPPED m_Overlapped;	//�ص��ṹ�壬�����첽I/Oģ�ͽ���ͨ�ŵĺ���
    IOType m_PostType;	      // ��ʶ�������������(��Ӧ�����ö��)

    SOCKET m_acceptSocket;	// ������������ʹ�õ�Socket
    WSABUF m_wsaBuf;			// WSA���͵Ļ����������ڸ��ص�������������
    char m_szBuffer[MAX_BUFFER_LEN];	// �����WSABUF�������ַ��Ļ�����
    DWORD m_nTotalBytes;		//�����ܵ��ֽ���
    DWORD m_nSentBytes;	     //�Ѿ����͵��ֽ�������δ��������������Ϊ0

    //���캯��
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

    //��������
    ~IoContext()
    {
        if (m_acceptSocket != INVALID_SOCKET)
        {
            m_acceptSocket = INVALID_SOCKET;
        }
    }

    //���û��������ݣ���IOContext���ظ�ʹ��ʱ��Ҫ�����һ�ε��������ݡ�
    void ResetBuffer()
    {
        ZeroMemory(m_szBuffer, MAX_BUFFER_LEN);
        m_wsaBuf.len = MAX_BUFFER_LEN;
        m_wsaBuf.buf = m_szBuffer;
    }
};

struct SocketContext   // PER_SOCKET_CONTEXT�ṹ������
{
    SOCKET m_Socket;		  // ÿһ���ͻ������ӵ�Socket
    SOCKADDR_IN m_ClientAddr; // �ͻ��˵ĵ�ַ
    vector<IoContext*> m_arrayIoContext;   // ������ Socket ������� IO ������

    //���캯��
    SocketContext()
    {
        m_Socket = INVALID_SOCKET;
        memset(&m_ClientAddr, 0, sizeof(m_ClientAddr));
    }

    //��������
    ~SocketContext()
    {
        if (m_Socket != INVALID_SOCKET)
        {
            closesocket(m_Socket); //!
            m_Socket = INVALID_SOCKET;
        }
        // �ͷŵ����е�IO����������
        for (size_t i = 0; i < m_arrayIoContext.size(); i++)
        {
            delete m_arrayIoContext.at(i);
        }
        m_arrayIoContext.clear();
    }

    //�����׽��ֲ���ʱ�����ô˺������� IoContext ָ�루�����ڷ�װÿ�� IO �����������ı�����
    IoContext* GetNewIoContext()
    {
        IoContext* p = new IoContext;
        m_arrayIoContext.push_back(p);
        return p;
    }

    // ���������Ƴ�һ��ָ����IoContext
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
#endif  // �������뱣������