//�������̨Ӧ�ó������ڵ㡣
#include "Server_Base.h"

#include <cstdio> // for printf_s
#include <iostream> // for std::cout and std::endl
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

class ServerCallBack : public TCP_Server
{
public:
    // ������
    void OnConnectionAccepted(SocketContext* pSoContext) override
    {
        PrintMessage("A connection is accepted, current connections: %d",
            this->GetConnectCount());
    }

    // ���ӹر�
    void OnConnectionClosed(SocketContext* pSoContext) override
    {
        PrintMessage("A connection is closed, current connections: %d",
            this->GetConnectCount());
    }

    // �����Ϸ�������
    void OnConnectionError(SocketContext* pSoContext, int error) override
    {
       /* std::cerr << "A connection have an error: " << error << ", current connections: "
            << this->GetConnectCount() << std::endl;*/
    }

    // ���������
    void OnRecvCompleted(SocketContext* pSoContext, IoContext* pIoContext) override
    {
        this->PrintMessage("Recv data: %s", pIoContext->m_wsaBuf.buf);
        this->SendData(pSoContext, pIoContext); // ����������ɣ�ԭ�ⲻ������ȥ
    }

    // д�������
    void OnSendCompleted(SocketContext* pSoContext, IoContext* pIoContext) override
    {
        this->RecvData(pSoContext, pIoContext); // ����������ɣ���ʼ��������
    }
public:
    void  PrintMessage(const char* format, ...)
    {
        std::lock_guard<std::mutex> lock(this->g_mtx);  // lock the mutex

        // Get current time using system clock and convert it to localtime
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        tm local_time = *std::localtime(&t);

        // Output the date and time in a specific format
        std::cout << std::put_time(&local_time, "[%Y-%m-%d %H:%M:%S]") << ": ";

        // Process variable arguments using printf() family functions
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);

        // Add linebreak at end of every message
        std::cout << std::endl;
    }
private:
 
    std::mutex g_mtx;
};


int main()
{
    ServerCallBack* pServer = new ServerCallBack; // �����������ص�����

    int processId = GetCurrentProcessId(); // ��ȡ���� ID
    int threadId = GetCurrentThreadId(); // ��ȡ�߳� ID

    // ������� ID ���߳� ID
    pServer->PrintMessage("Current Process  Id: %d.", processId);
    pServer->PrintMessage("Current Thread   Id: %d.", threadId);

    if (pServer->Start(10240)) // ����������
    {
        // ��������˿ں�
        int portNumber = pServer->GetPort();
        pServer->PrintMessage("Server has started successfully on port %d.", portNumber);
    }
    else
    {
        pServer->PrintMessage("Failed to start server!");
        return 0;
    }

    HANDLE hEvent = ::CreateEvent(NULL, FALSE, FALSE, L"ShutdownEvent"); // ����һ���¼��������ڹرշ�����
    if (hEvent != NULL)
    {
        ::WaitForSingleObject(hEvent, INFINITE);
        ::CloseHandle(hEvent); // �ر��¼�
    }

    pServer->Stop(); // ֹͣ���з�����
    pServer->PrintMessage("The server has been closed."); // ������������ѹر�

    delete pServer; // ɾ������������

   
    return 0;
}
