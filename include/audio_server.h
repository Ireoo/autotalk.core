#pragma once

#include <vector>
#include <functional>
#include <string>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <atomic>
#include <map>
#ifdef _WIN32
#include <WinSock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#endif

class AudioServer {
public:
    AudioServer();
    ~AudioServer();

    // 初始化服务器
    bool initialize(int port = 3000);
    // 启动服务器
    bool start(std::function<void(const std::vector<float>&)> audioCallback);
    // 停止服务器
    void stop();
    
    // 发送实时语音识别结果给客户端
    void sendLiveResult(const std::string& text);
    // 发送完整句子识别结果给客户端
    void sendCompleteResult(const std::string& text);

private:
    // 客户端连接结构
    struct ClientConnection {
        #ifdef _WIN32
        SOCKET socket;
        #else
        int socket;
        #endif
        bool connected;
    };
    
    // 处理新连接
    void acceptConnections();
    // 处理客户端数据
    void handleClientData(ClientConnection& client);
    // 处理音频数据的线程函数
    void processAudioQueue();
    // 向指定客户端发送数据
    bool sendToClient(ClientConnection& client, const std::string& message);
    // 向所有客户端广播数据
    void broadcastToAll(const std::string& message);
    
    // 网络套接字
    #ifdef _WIN32
    SOCKET serverSocket_;
    #else
    int serverSocket_;
    #endif
    
    // 客户端连接列表
    std::vector<ClientConnection> clients_;
    std::mutex clientsMutex_;
    
    // 音频数据回调函数
    std::function<void(const std::vector<float>&)> audioCallback_;
    
    // 音频数据队列和互斥锁
    std::queue<std::vector<float>> audioQueue_;
    std::mutex audioQueueMutex_;
    
    // 控制变量
    std::atomic<bool> running_;
    int serverPort_;
    
    // 处理线程
    std::thread acceptThread_;
    std::thread processingThread_;
}; 