## Sample_Server

### 项目概述
Sample_Server 是一个使用 C++ 编写的 TCP/IP 网络服务器示例。其主要功能在于监听来自客户端的连接，并负责数据交换。

为了实现这些功能，该服务器应用了多线程技术，并采用了类似于 Telnet 客户端的命令行文本协议来处理和传递消息。服务器将解析从客户端发送的消息，并根据前缀字符进行动作路由，与之匹配的方法将被调用。

### 源代码文件结构
main.cpp：应用程序入口，执行服务器初始化操作并启动多线程处理。
Server_Base.h：定义 Server 类及其公共方法和属性。
Server_Def.h：包含常量、宏和服务器常规设置。
Server_Core.cpp：定义 Server 的核心方法和工作原理。
其中，提供了 ServerCallBack 基类，定义了 Start() 方法，以便能够直接从继承的子类中访问。

### 下面是示例代码用于创建新的 Server 实例：
 ServerCallBack* pServer = new ServerCallBack; // 创建服务器回调对象
 
 pServer->Start(10240));                       // 启动服务器
