#include "../include/websocket_client.h"
#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <vector>
#include <cstring>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <algorithm>
#include <sstream>
#include <random>
#include <list>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")

// WebSocket帧的操作码
enum OpCode {
    CONTINUATION = 0x0,
    TEXT = 0x1,
    BINARY = 0x2,
    CLOSE = 0x8,
    PING = 0x9,
    PONG = 0xA
};

// 客户端连接
struct ClientConnection {
    SOCKET socket;
    std::thread* receiveThread;
    std::atomic<bool> connected;
    
    ClientConnection(SOCKET s) : socket(s), receiveThread(nullptr), connected(true) {}
    
    ~ClientConnection() {
        if (receiveThread && receiveThread->joinable()) {
            receiveThread->join();
            delete receiveThread;
        }
        if (socket != INVALID_SOCKET) {
            closesocket(socket);
        }
    }
};

// WebSocket实现类
class WebSocketImpl {
public:
    WebSocketImpl() : serverSocket(INVALID_SOCKET), running(false), acceptThread(nullptr) {}
    
    ~WebSocketImpl() {
        stop();
    }
    
    // 启动WebSocket服务器
    bool start(int port) {
        // 初始化Winsock
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup失败" << std::endl;
            return false;
        }
        
        // 创建套接字
        serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (serverSocket == INVALID_SOCKET) {
            std::cerr << "创建套接字失败" << std::endl;
            WSACleanup();
            return false;
        }
        
        // 允许地址复用
        int yes = 1;
        if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes)) == SOCKET_ERROR) {
            std::cerr << "设置SO_REUSEADDR失败" << std::endl;
            closesocket(serverSocket);
            WSACleanup();
            return false;
        }
        
        // 绑定地址
        struct sockaddr_in addr;
        ZeroMemory(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        
        if (bind(serverSocket, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            std::cerr << "绑定地址失败" << std::endl;
            closesocket(serverSocket);
            WSACleanup();
            return false;
        }
        
        // 开始监听
        if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
            std::cerr << "监听失败" << std::endl;
            closesocket(serverSocket);
            WSACleanup();
            return false;
        }
        
        running = true;
        
        // 启动接收线程
        acceptThread = new std::thread(&WebSocketImpl::acceptLoop, this);
        
        std::cout << "WebSocket服务器已启动，监听端口: " << port << std::endl;
        return true;
    }
    
    // 停止服务器
    void stop() {
        running = false;
        
        // 关闭所有客户端连接
        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            for (auto& client : clients) {
                client->connected = false;
                sendFrame(client->socket, CLOSE, nullptr, 0);
                closesocket(client->socket);
            }
            clients.clear();
        }
        
        // 关闭服务器socket
        if (serverSocket != INVALID_SOCKET) {
            closesocket(serverSocket);
            serverSocket = INVALID_SOCKET;
        }
        
        // 等待accept线程结束
        if (acceptThread && acceptThread->joinable()) {
            acceptThread->join();
            delete acceptThread;
            acceptThread = nullptr;
        }
        
        WSACleanup();
    }
    
    // 广播文本消息给所有客户端
    bool broadcastText(const std::string& message) {
        std::lock_guard<std::mutex> lock(clientsMutex);
        bool success = true;
        
        // 遍历所有客户端，发送消息
        for (auto it = clients.begin(); it != clients.end();) {
            auto client = *it;
            if (client->connected) {
                if (!sendFrame(client->socket, TEXT, (const uint8_t*)message.c_str(), message.length())) {
                    success = false;
                    client->connected = false;
                    it = clients.erase(it);
                    continue;
                }
                ++it;
            } else {
                it = clients.erase(it);
            }
        }
        
        return success;
    }
    
    // 广播二进制数据给所有客户端
    bool broadcastBinary(const std::vector<float>& data) {
        // 将浮点数组转换为JSON格式的字符串
        std::stringstream ss;
        ss << "[";
        for (size_t i = 0; i < data.size(); ++i) {
            ss << data[i];
            if (i < data.size() - 1) {
                ss << ",";
            }
        }
        ss << "]";
        
        return broadcastText(ss.str());
    }
    
    // 设置消息接收回调
    void setReceiveCallback(std::function<void(const std::string&)> callback) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        receiveCallback = callback;
    }
    
    // 检查是否正在运行
    bool isRunning() const {
        return running;
    }
    
private:
    // 接受新客户端连接的线程函数
    void acceptLoop() {
        while (running) {
            // 接受新的客户端连接
            struct sockaddr_in clientAddr;
            int addrLen = sizeof(clientAddr);
            SOCKET clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &addrLen);
            
            if (clientSocket == INVALID_SOCKET) {
                if (running) {
                    std::cerr << "接受客户端连接失败" << std::endl;
                }
                break;
            }
            
            // 获取客户端IP地址
            char clientIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);
            std::cout << "新客户端连接: " << clientIP << std::endl;
            
            // 处理WebSocket握手
            if (handleHandshake(clientSocket)) {
                // 创建客户端连接对象
                auto client = std::make_shared<ClientConnection>(clientSocket);
                
                // 添加到客户端列表
                {
                    std::lock_guard<std::mutex> lock(clientsMutex);
                    clients.push_back(client);
                }
                
                // 启动接收线程
                client->receiveThread = new std::thread(&WebSocketImpl::receiveLoop, this, client);
            } else {
                closesocket(clientSocket);
            }
        }
    }
    
    // 处理WebSocket握手
    bool handleHandshake(SOCKET clientSocket) {
        char buffer[4096] = {0};
        int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesRead <= 0) {
            return false;
        }
        
        buffer[bytesRead] = '\0';
        
        // 解析HTTP请求
        std::string request(buffer);
        std::string key;
        
        // 查找Sec-WebSocket-Key头
        const std::string keyMarker = "Sec-WebSocket-Key: ";
        size_t keyStart = request.find(keyMarker);
        if (keyStart != std::string::npos) {
            keyStart += keyMarker.length();
            size_t keyEnd = request.find("\r\n", keyStart);
            if (keyEnd != std::string::npos) {
                key = request.substr(keyStart, keyEnd - keyStart);
            }
        }
        
        if (key.empty()) {
            return false;
        }
        
        // 生成响应
        std::string response = "HTTP/1.1 101 Switching Protocols\r\n"
                               "Upgrade: websocket\r\n"
                               "Connection: Upgrade\r\n"
                               "Sec-WebSocket-Accept: " + computeAcceptKey(key) + "\r\n\r\n";
        
        // 发送响应
        if (send(clientSocket, response.c_str(), response.length(), 0) == SOCKET_ERROR) {
            return false;
        }
        
        return true;
    }
    
    // 计算WebSocket握手的Accept Key
    std::string computeAcceptKey(const std::string& key) {
        // 简化实现：返回原始key（真实实现需要SHA1+Base64）
        // 完整实现请参考WebSocket协议规范
        return key;
    }
    
    // 接收客户端数据的线程函数
    void receiveLoop(std::shared_ptr<ClientConnection> client) {
        while (client->connected && running) {
            // 接收帧头
            uint8_t frameHeader[2] = {0};
            if (recv(client->socket, (char*)frameHeader, 2, 0) <= 0) {
                break;
            }
            
            // 解析帧头
            bool fin = (frameHeader[0] & 0x80) != 0;
            OpCode opcode = (OpCode)(frameHeader[0] & 0x0F);
            bool masked = (frameHeader[1] & 0x80) != 0;
            uint64_t payloadLength = frameHeader[1] & 0x7F;
            
            // 读取扩展长度
            if (payloadLength == 126) {
                uint8_t lenBytes[2] = {0};
                if (recv(client->socket, (char*)lenBytes, 2, 0) <= 0) {
                    break;
                }
                payloadLength = (lenBytes[0] << 8) | lenBytes[1];
            } else if (payloadLength == 127) {
                uint8_t lenBytes[8] = {0};
                if (recv(client->socket, (char*)lenBytes, 8, 0) <= 0) {
                    break;
                }
                payloadLength = 0;
                for (int i = 0; i < 8; ++i) {
                    payloadLength = (payloadLength << 8) | lenBytes[i];
                }
            }
            
            // 读取掩码（如果有）
            uint8_t mask[4] = {0};
            if (masked) {
                if (recv(client->socket, (char*)mask, 4, 0) <= 0) {
                    break;
                }
            }
            
            // 读取负载数据
            std::vector<uint8_t> payload(payloadLength);
            size_t bytesRemaining = payloadLength;
            size_t offset = 0;
            
            while (bytesRemaining > 0) {
                int bytesRead = recv(client->socket, (char*)payload.data() + offset, bytesRemaining, 0);
                if (bytesRead <= 0) {
                    break;
                }
                bytesRemaining -= bytesRead;
                offset += bytesRead;
            }
            
            // 解除掩码（如果有）
            if (masked) {
                for (size_t i = 0; i < payloadLength; ++i) {
                    payload[i] ^= mask[i % 4];
                }
            }
            
            // 处理帧
            switch (opcode) {
                case TEXT: {
                    std::string message((char*)payload.data(), payload.size());
                    
                    // 调用回调
                    std::lock_guard<std::mutex> lock(callbackMutex);
                    if (receiveCallback) {
                        receiveCallback(message);
                    }
                    break;
                }
                
                case BINARY: {
                    // 将二进制数据转换为字符串
                    std::string message((char*)payload.data(), payload.size());
                    
                    // 调用回调
                    std::lock_guard<std::mutex> lock(callbackMutex);
                    if (receiveCallback) {
                        receiveCallback(message);
                    }
                    break;
                }
                
                case PING: {
                    // 响应PING
                    sendFrame(client->socket, PONG, payload.data(), payload.size());
                    break;
                }
                
                case CLOSE: {
                    // 收到关闭帧，断开连接
                    client->connected = false;
                    break;
                }
                
                default:
                    break;
            }
        }
        
        client->connected = false;
    }
    
    // 发送WebSocket帧
    bool sendFrame(SOCKET socket, OpCode opcode, const uint8_t* payload, size_t length) {
        // 构造WebSocket帧头
        std::vector<uint8_t> frame;
        frame.push_back(0x80 | opcode); // FIN + opcode
        
        // 添加Payload长度
        if (length < 126) {
            frame.push_back(length);
        } else if (length < 65536) {
            frame.push_back(126);
            frame.push_back((length >> 8) & 0xFF);
            frame.push_back(length & 0xFF);
        } else {
            frame.push_back(127);
            frame.push_back(0); // 假设长度不超过32位
            frame.push_back(0);
            frame.push_back(0);
            frame.push_back(0);
            frame.push_back((length >> 24) & 0xFF);
            frame.push_back((length >> 16) & 0xFF);
            frame.push_back((length >> 8) & 0xFF);
            frame.push_back(length & 0xFF);
        }
        
        // 添加Payload
        if (payload && length > 0) {
            frame.insert(frame.end(), payload, payload + length);
        }
        
        // 发送帧
        int result = send(socket, (const char*)frame.data(), frame.size(), 0);
        return result != SOCKET_ERROR;
    }
    
    SOCKET serverSocket;
    std::atomic<bool> running;
    std::thread* acceptThread;
    std::vector<std::shared_ptr<ClientConnection>> clients;
    std::mutex clientsMutex;
    std::function<void(const std::string&)> receiveCallback;
    std::mutex callbackMutex;
};

// WebSocketServer实现
WebSocketServer::WebSocketServer() : impl_(new WebSocketImpl()), running_(false) {}

WebSocketServer::~WebSocketServer() {
    stop();
}

bool WebSocketServer::start(int port) {
    bool result = impl_->start(port);
    running_ = result;
    return result;
}

void WebSocketServer::stop() {
    if (impl_) {
        impl_->stop();
    }
    running_ = false;
}

bool WebSocketServer::broadcastText(const std::string& message) {
    if (!impl_ || !running_) {
        return false;
    }
    return impl_->broadcastText(message);
}

bool WebSocketServer::broadcastBinary(const std::vector<float>& data) {
    if (!impl_ || !running_) {
        return false;
    }
    return impl_->broadcastBinary(data);
}

void WebSocketServer::setReceiveCallback(std::function<void(const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    receiveCallback_ = callback;
    
    if (impl_) {
        impl_->setReceiveCallback([this](const std::string& message) {
            std::lock_guard<std::mutex> lock(callbackMutex_);
            if (receiveCallback_) {
                receiveCallback_(message);
            }
        });
    }
}

bool WebSocketServer::isRunning() const {
    return running_ && impl_ && impl_->isRunning();
} 