#include "../include/audio_server.h"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cstring>

#define BUFFER_SIZE 8192

AudioServer::AudioServer() 
    : running_(false), serverPort_(3000)
#ifdef _WIN32
    , serverSocket_(INVALID_SOCKET)
#else
    , serverSocket_(-1)
#endif
{
#ifdef _WIN32
    // 初始化Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup 失败" << std::endl;
    }
#endif
}

AudioServer::~AudioServer() {
    stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool AudioServer::initialize(int port) {
    serverPort_ = port;
    return true;
}

bool AudioServer::start(std::function<void(const std::vector<float>&)> audioCallback) {
    if(running_) {
        std::cerr << "服务器已经在运行" << std::endl;
        return false;
    }
    
    audioCallback_ = audioCallback;
    
#ifdef _WIN32
    serverSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket_ == INVALID_SOCKET) {
        std::cerr << "创建套接字失败: " << WSAGetLastError() << std::endl;
        return false;
    }
#else
    serverSocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket_ < 0) {
        std::cerr << "创建套接字失败" << std::endl;
        return false;
    }
#endif

    // 允许地址重用
    int opt = 1;
#ifdef _WIN32
    if (setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) != 0) {
        std::cerr << "设置套接字选项失败: " << WSAGetLastError() << std::endl;
        return false;
    }
#else
    if (setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0) {
        std::cerr << "设置套接字选项失败" << std::endl;
        return false;
    }
#endif

    // 绑定地址
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(serverPort_);

#ifdef _WIN32
    if (bind(serverSocket_, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "绑定套接字失败: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket_);
        return false;
    }
#else
    if (bind(serverSocket_, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "绑定套接字失败" << std::endl;
        close(serverSocket_);
        return false;
    }
#endif

    // 监听连接
#ifdef _WIN32
    if (listen(serverSocket_, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "监听套接字失败: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket_);
        return false;
    }
#else
    if (listen(serverSocket_, 5) < 0) {
        std::cerr << "监听套接字失败" << std::endl;
        close(serverSocket_);
        return false;
    }
#endif

    running_ = true;
    
    // 启动接受连接的线程
    acceptThread_ = std::thread(&AudioServer::acceptConnections, this);
    
    // 启动音频处理线程
    processingThread_ = std::thread(&AudioServer::processAudioQueue, this);
    
    std::cout << "服务器已启动，监听端口: " << serverPort_ << std::endl;
    return true;
}

void AudioServer::stop() {
    if(!running_) {
        return;
    }
    
    running_ = false;
    
    // 关闭所有客户端连接
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for(auto& client : clients_) {
            if(client.connected) {
#ifdef _WIN32
                closesocket(client.socket);
#else
                close(client.socket);
#endif
                client.connected = false;
            }
        }
        clients_.clear();
    }
    
    // 关闭服务器套接字
#ifdef _WIN32
    if(serverSocket_ != INVALID_SOCKET) {
        closesocket(serverSocket_);
        serverSocket_ = INVALID_SOCKET;
    }
#else
    if(serverSocket_ >= 0) {
        close(serverSocket_);
        serverSocket_ = -1;
    }
#endif
    
    // 等待线程结束
    if(acceptThread_.joinable()) {
        acceptThread_.join();
    }
    if(processingThread_.joinable()) {
        processingThread_.join();
    }
    
    std::cout << "服务器已停止" << std::endl;
}

void AudioServer::acceptConnections() {
    while(running_) {
        // 接受新连接
        sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);
        
#ifdef _WIN32
        SOCKET clientSocket = accept(serverSocket_, (SOCKADDR*)&clientAddr, &clientAddrLen);
        if(clientSocket == INVALID_SOCKET) {
            if(running_) {
                std::cerr << "接受连接失败: " << WSAGetLastError() << std::endl;
            }
            continue;
        }
#else
        int clientSocket = accept(serverSocket_, (struct sockaddr*)&clientAddr, &clientAddrLen);
        if(clientSocket < 0) {
            if(running_) {
                std::cerr << "接受连接失败" << std::endl;
            }
            continue;
        }
#endif

        // 获取客户端IP地址
        char clientIP[INET_ADDRSTRLEN];
#ifdef _WIN32
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
#else
        inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);
#endif
        
        std::cout << "新客户端连接: " << clientIP << ":" << ntohs(clientAddr.sin_port) << std::endl;
        
        // 添加到客户端列表
        ClientConnection client;
        client.socket = clientSocket;
        client.connected = true;
        
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            clients_.push_back(client);
        }
        
        // 向客户端发送欢迎消息
        sendToClient(client, "SERVER_READY:Server is ready to receive audio data");
        
        // 为每个客户端创建处理线程
        std::thread clientThread(&AudioServer::handleClientData, this, std::ref(client));
        clientThread.detach();
    }
}

void AudioServer::handleClientData(ClientConnection& client) {
    char buffer[BUFFER_SIZE];
    
    while(running_ && client.connected) {
        // 接收数据
#ifdef _WIN32
        int bytesRead = recv(client.socket, buffer, BUFFER_SIZE, 0);
        if(bytesRead == SOCKET_ERROR || bytesRead == 0) {
            if(running_) {
                std::cerr << "接收数据失败或客户端断开连接" << std::endl;
            }
            break;
        }
#else
        int bytesRead = recv(client.socket, buffer, BUFFER_SIZE, 0);
        if(bytesRead <= 0) {
            if(running_) {
                std::cerr << "接收数据失败或客户端断开连接" << std::endl;
            }
            break;
        }
#endif

        // 处理接收到的数据
        if(bytesRead > 0) {
            // 判断是否是音频数据
            if(bytesRead >= 8 && strncmp(buffer, "AUDIO:", 6) == 0) {
                // 将二进制数据转换为浮点数组
                const size_t audioDataOffset = 6; // "AUDIO:"的长度
                const size_t floats_count = (bytesRead - audioDataOffset) / sizeof(float);
                
                if(floats_count > 0) {
                    std::vector<float> audio_buffer(floats_count);
                    memcpy(audio_buffer.data(), buffer + audioDataOffset, bytesRead - audioDataOffset);
                    
                    // 加入处理队列
                    std::lock_guard<std::mutex> lock(audioQueueMutex_);
                    audioQueue_.push(std::move(audio_buffer));
                }
            }
        }
    }
    
    // 客户端断开连接，从列表中移除
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto it = std::find_if(clients_.begin(), clients_.end(), 
            [&client](const ClientConnection& c) { 
                return c.socket == client.socket; 
            });
        
        if(it != clients_.end()) {
#ifdef _WIN32
            closesocket(it->socket);
#else
            close(it->socket);
#endif
            it->connected = false;
            clients_.erase(it);
        }
    }
}

void AudioServer::processAudioQueue() {
    while(running_) {
        std::vector<float> audio_data;
        
        {
            std::lock_guard<std::mutex> lock(audioQueueMutex_);
            if(!audioQueue_.empty()) {
                audio_data = std::move(audioQueue_.front());
                audioQueue_.pop();
            }
        }
        
        if(!audio_data.empty() && audioCallback_) {
            audioCallback_(audio_data);
        }
        
        // 避免CPU占用过高
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

bool AudioServer::sendToClient(ClientConnection& client, const std::string& message) {
    if(!client.connected) {
        return false;
    }
    
#ifdef _WIN32
    int bytesSent = send(client.socket, message.c_str(), static_cast<int>(message.length()), 0);
    if(bytesSent == SOCKET_ERROR) {
        std::cerr << "发送数据失败: " << WSAGetLastError() << std::endl;
        return false;
    }
#else
    int bytesSent = send(client.socket, message.c_str(), message.length(), 0);
    if(bytesSent < 0) {
        std::cerr << "发送数据失败" << std::endl;
        return false;
    }
#endif
    
    return true;
}

void AudioServer::broadcastToAll(const std::string& message) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for(auto& client : clients_) {
        if(client.connected) {
            sendToClient(client, message);
        }
    }
}

void AudioServer::sendLiveResult(const std::string& text) {
    if(!text.empty()) {
        std::string prefixed_text = "L:" + text;
        broadcastToAll(prefixed_text);
    }
}

void AudioServer::sendCompleteResult(const std::string& text) {
    if(!text.empty()) {
        std::string prefixed_text = "T:" + text;
        broadcastToAll(prefixed_text);
    }
} 