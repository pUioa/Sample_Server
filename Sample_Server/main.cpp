//定义控制台应用程序的入口点。
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
    // 新连接
    void OnConnectionAccepted(SocketContext* pSoContext) override
    {
        PrintMessage("A connection is accepted, current connections: %d",
            this->GetConnectCount());
    }

    // 连接关闭
    void OnConnectionClosed(SocketContext* pSoContext) override
    {
        PrintMessage("A connection is closed, current connections: %d",
            this->GetConnectCount());
    }

    // 连接上发生错误
    void OnConnectionError(SocketContext* pSoContext, int error) override
    {
       /* std::cerr << "A connection have an error: " << error << ", current connections: "
            << this->GetConnectCount() << std::endl;*/
    }

    // 读操作完成
    void OnRecvCompleted(SocketContext* pSoContext, IoContext* pIoContext) override
    {
        this->PrintMessage("Recv data: %s", pIoContext->m_wsaBuf.buf);
        this->SendData(pSoContext, pIoContext); // 接收数据完成，原封不动发回去
    }

    // 写操作完成
    void OnSendCompleted(SocketContext* pSoContext, IoContext* pIoContext) override
    {
        this->RecvData(pSoContext, pIoContext); // 发送数据完成，开始接收数据
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
    ServerCallBack* pServer = new ServerCallBack; // 创建服务器回调对象

    int processId = GetCurrentProcessId(); // 获取进程 ID
    int threadId = GetCurrentThreadId(); // 获取线程 ID

    // 输出进程 ID 和线程 ID
    pServer->PrintMessage("Current Process  Id: %d.", processId);
    pServer->PrintMessage("Current Thread   Id: %d.", threadId);

    if (pServer->Start(10240)) // 启动服务器
    {
        // 输出监听端口号
        int portNumber = pServer->GetPort();
        pServer->PrintMessage("Server has started successfully on port %d.", portNumber);
    }
    else
    {
        pServer->PrintMessage("Failed to start server!");
        return 0;
    }

    HANDLE hEvent = ::CreateEvent(NULL, FALSE, FALSE, L"ShutdownEvent"); // 创建一个事件对象，用于关闭服务器
    if (hEvent != NULL)
    {
        ::WaitForSingleObject(hEvent, INFINITE);
        ::CloseHandle(hEvent); // 关闭事件
    }

    pServer->Stop(); // 停止运行服务器
    pServer->PrintMessage("The server has been closed."); // 输出：服务器已关闭

    delete pServer; // 删除服务器对象

   
    return 0;
}
