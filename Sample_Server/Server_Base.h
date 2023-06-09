#pragma once
#include "Server_Def.h"
class TCP_Server;			//�������Ļ�ȡ��ָ��ʵ������Call
struct WorkerThreadParam
{
	TCP_Server* pServer;		//��ָ�룬���ڵ������еĺ���

	int nThreadNo;				//�̱߳��
	int nThreadId;				//�߳�ID
	HANDLE hThread;				//�߳̾��
};

class TCP_Server {
public:
    // �����������ӽ���ʱ���õ�����ص�����
    virtual void OnConnectionAccepted(SocketContext* pSoContext) {};
    // �����������ӹر�ʱ���õ�����ص�����
    virtual void OnConnectionClosed(SocketContext* pSoContext) {};
    // �ڷ������˽��������ڼ䷢������ʱ���õ�����ص�����
    virtual void OnConnectionError(SocketContext* pSoContext, int error) {};
    // �����ݽ��ղ������ʱ���õ�����ص�����
    virtual void OnRecvCompleted(SocketContext* pSoContext, IoContext* pIoContext) {};
    // �����ݷ��Ͳ������ʱ���õ�����ص�����
    virtual void OnSendCompleted(SocketContext* pSoContext, IoContext* pIoContext) {};

    // �������ݵ�ָ���׽���
    bool SendData(SocketContext* pSoContext, char* data, int size);
    // ���ͻ������е��������ݵ�ָ���׽���
    bool SendData(SocketContext* pSoContext, IoContext* pIoContext);
    // �������ݴ�ָ���׽���
    bool RecvData(SocketContext* pSoContext, IoContext* pIoContext);
    // ���ص�ǰ�׽��ֳ������
    DWORD GetErrorCount();
    // ���ص�ǰͶ�ݵ�Accept����
    DWORD GetAcceptPostCount();
    // ���ص�ǰ��������
    DWORD GetConnectCount();
    // ���ط����������˿ں�
    DWORD GetPort();

private:
    // ��������ȫ�ر��¼����
    HANDLE m_hShutdownEvent;
    // ��ɶ˿ھ��
    HANDLE m_hIOCompletionPort;
    // �������߳̾��ָ��
    HANDLE* m_phWorkerThreads;
    // ���ɵĹ������߳�����
    DWORD m_nThreads;
    // ��������ַIP
    std::string m_strIP;
    // �����������˿ں�
    DWORD m_nPort;
    // ���ӱ�����
    std::mutex m_ClientsContexts_Mutex;
    // ���󻥳���
    std::mutex m_HandleError_Mutex;
    // ���ӱ�
    std::list<SocketContext*> m_ClientsContexts;
    // �������׽�����������Ϣ
    SocketContext* m_pListenContext;
    // ��ǰͶ�ݵ�Accept����
    DWORD m_AcceptPostCount;
    // ��ǰ��������
    DWORD m_ConnectCount;
    // ��ǰ��������
    DWORD m_ErrorCount;
    // ������AcceptEx�йص�socket��ַ��Ϣ����ָ��
    LPFN_GETACCEPTEXSOCKADDRS m_lpfnGetAcceptExSockAddrs;
    // AcceptEx����ָ��
    LPFN_ACCEPTEX m_lpfnAcceptEx;

    // ��ȡCPU��������
    DWORD GetNumOfProcessors();
    // ȷ���׽����Ƿ���Ч
    bool IsSocketAlive(SOCKET s);
    // �����׽��ִ���
    bool HandleError(SocketContext* pSoContext, const DWORD& dwErr);
    // �������̴߳�����
    static DWORD WINAPI WorkerThread(LPVOID lpParam);
    // Ͷ��AcceptEx����
    bool PostAccept(IoContext* pIoContext);
    // ִ��AcceptEx�����������¿ͻ��˲���ʼ���ݴ���
    bool DoAccept(SocketContext* pSoContext, IoContext* pIoContext);
    // Ͷ��Recv��������������
    bool PostRecv(SocketContext* pSoContext, IoContext* pIoContext);
    // ��ָ���׽��ֽ�������
    bool DoRecv(SocketContext* pSoContext, IoContext* pIoContext);
    // Ͷ��Send���������׽����Ϸ�������
    bool PostSend(SocketContext* pSoContext, IoContext* pIoContext);
    // ���������е����ݷ��͵��׽�����
    bool DoSend(SocketContext* pSoContext, IoContext* pIoContext);
    // �ر�ָ���׽���
    bool DoClose(SocketContext* pSoContext);
    // ����ɶ˿ڹ���
    bool AssociateWithIOCP(SocketContext* pSoContext);
    // ��ͻ��������б�������µĿͻ�����������Ϣ
    void AddToClientsContextsList(SocketContext* pSoContext);
    // �ӿͻ��������б����Ƴ��ѹرջ�ʧЧ�Ŀͻ���
    void RemoveClientsFormContextsList(SocketContext* pSoContext);
    // ��տͻ��������б�
    void ClearClientsContextsList();

public:
    // Ĭ�Ϲ��캯��
    TCP_Server();
    // ��������
    ~TCP_Server();
    // ��ʼ��Socket��
    bool LoadSocketLib();
    // ж��Socket��
    void UnloadSocketLib();
    // ��ʼ��IOCP����
    bool InitializeIOCP();
    // ��ʼ�������׽���
    bool InitializeListenSocket();
    // ��������������ʼ������������
    bool Start(DWORD p_Port);
    // ֹͣ����������������������Դ
    void DeInitialize();
    // ��ȫֹͣ���������ڵȴ��߳�ֱ���˳�
    void Stop();
};
