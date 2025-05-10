#include "../include/websocket_client.h"
#include "../include/voiceprint_recognition.h"
#include <iostream>
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
#include <deque>
#include <chrono>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef SOCKET socket_t;
    #define SOCKET_ERROR_VALUE SOCKET_ERROR
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
    #define CLOSE_SOCKET(s) closesocket(s)
    #define SOCKET_LAST_ERROR WSAGetLastError()
    #define SOCKET_EWOULDBLOCK WSAEWOULDBLOCK
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <netdb.h>
    #include <errno.h>
    typedef int socket_t;
    #define SOCKET_ERROR_VALUE -1
    #define INVALID_SOCKET_VALUE -1
    #define CLOSE_SOCKET(s) close(s)
    #define SOCKET_LAST_ERROR errno
    #define SOCKET_EWOULDBLOCK EWOULDBLOCK
#endif

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
    socket_t socket;
    std::thread* receiveThread;
    std::atomic<bool> connected;
    std::string clientId;
    std::vector<float> audio_chunk;
    std::vector<float>::iterator audio_chunk_begin;
    size_t audio_chunk_last;
    std::string current_speaker;  // 添加当前说话人字段
    
    ClientConnection(socket_t s) : socket(s), receiveThread(nullptr), connected(true), audio_chunk_last(0), current_speaker("unknown") {
        // 生成随机的客户端ID
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(10000, 99999);
        clientId = "user_" + std::to_string(dis(gen));
        
        // 初始化音频数据
        audio_chunk_begin = audio_chunk.begin();
    }
    
    ~ClientConnection() {
        if (receiveThread && receiveThread->joinable()) {
            receiveThread->join();
            delete receiveThread;
        }
        if (socket != INVALID_SOCKET_VALUE) {
            CLOSE_SOCKET(socket);
        }
    }
};

// WebSocket实现类
class WebSocketImpl {
public:
    WebSocketImpl() : serverSocket(INVALID_SOCKET_VALUE), running(false), acceptThread(nullptr), cleanupThreadRunning(false) {}
    
    ~WebSocketImpl() {
        stop();
    }
    
    // 启动WebSocket服务器
    bool start(int port) {
#ifdef _WIN32
        // 初始化Winsock
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup失败" << std::endl;
            return false;
        }
#endif
        
        // 创建套接字
        serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (serverSocket == INVALID_SOCKET_VALUE) {
            std::cerr << "创建套接字失败" << std::endl;
#ifdef _WIN32
            WSACleanup();
#endif
            return false;
        }
        
        // 允许地址复用
        int yes = 1;
        if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes)) == SOCKET_ERROR_VALUE) {
            std::cerr << "设置SO_REUSEADDR失败" << std::endl;
            CLOSE_SOCKET(serverSocket);
#ifdef _WIN32
            WSACleanup();
#endif
            return false;
        }
        
        // 绑定地址
        struct sockaddr_in addr;
#ifdef _WIN32
        ZeroMemory(&addr, sizeof(addr));
#else
        memset(&addr, 0, sizeof(addr));
#endif
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        
        if (bind(serverSocket, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR_VALUE) {
            std::cerr << "绑定地址失败" << std::endl;
            CLOSE_SOCKET(serverSocket);
#ifdef _WIN32
            WSACleanup();
#endif
            return false;
        }
        
        // 开始监听
        if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR_VALUE) {
            std::cerr << "监听失败" << std::endl;
            CLOSE_SOCKET(serverSocket);
#ifdef _WIN32
            WSACleanup();
#endif
            return false;
        }
        
        running = true;
        cleanupThreadRunning = true;
        
        // 启动接收线程
        acceptThread = new std::thread(&WebSocketImpl::acceptLoop, this);
        
        // 启动清理线程
        cleanupThread = new std::thread(&WebSocketImpl::cleanupLoop, this);
        
        std::cout << "WebSocket服务器已启动，监听端口: " << port << std::endl;
        return true;
    }
    
    // 停止服务器
    void stop() {
        running = false;
        cleanupThreadRunning = false;
        
        // 关闭所有客户端连接
        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            for (auto& client : clients) {
                client->connected = false;
                
                // 清理用户的音频数据
                client->audio_chunk.clear();
                client->audio_chunk_begin = client->audio_chunk.begin();
                client->audio_chunk_last = 0;
                
                sendFrame(client->socket, CLOSE, nullptr, 0);
                CLOSE_SOCKET(client->socket);
            }
            clients.clear();
            disconnectedClients.clear();
        }
        
        // 关闭服务器socket
        if (serverSocket != INVALID_SOCKET_VALUE) {
            CLOSE_SOCKET(serverSocket);
            serverSocket = INVALID_SOCKET_VALUE;
        }
        
        // 等待accept线程结束
        if (acceptThread && acceptThread->joinable()) {
            acceptThread->join();
            delete acceptThread;
            acceptThread = nullptr;
        }
        
        // 等待清理线程结束
        if (cleanupThread && cleanupThread->joinable()) {
            cleanupThread->join();
            delete cleanupThread;
            cleanupThread = nullptr;
        }
        
#ifdef _WIN32
        WSACleanup();
#endif
    }
    
    // 广播文本消息给所有客户端
    bool broadcastText(const std::string& message, const std::string& targetClientId = "") {
        cleanupDisconnectedClients();
        
        std::lock_guard<std::mutex> lock(clientsMutex);
        bool success = true;
        
        // 检查是否需要发送给特定客户端
        if (!targetClientId.empty()) {
            // 查找特定客户端
            auto it = std::find_if(clients.begin(), clients.end(), [&targetClientId](const std::shared_ptr<ClientConnection>& client) {
                return client->clientId == targetClientId && client->connected;
            });
            
            if (it != clients.end()) {
                // 发送给特定客户端
                auto client = *it;
                if (!sendFrame(client->socket, TEXT, (const uint8_t*)message.c_str(), message.length())) {
                    success = false;
                    client->connected = false;
                }
            } else {
                // 未找到指定客户端
                success = false;
            }
        } else {
            // 广播给所有客户端
            for (auto it = clients.begin(); it != clients.end();) {
                auto client = *it;
                if (client->connected) {
                    if (!sendFrame(client->socket, TEXT, (const uint8_t*)message.c_str(), message.length())) {
                        success = false;
                        client->connected = false;
                        // 安全地标记客户端为断开状态，而不是立即删除
                        std::lock_guard<std::mutex> disconnectLock(disconnectedClientsMutex);
                        disconnectedClients.push_back(client);
                        ++it;
                    } else {
                        ++it;
                    }
                } else {
                    // 安全地标记客户端为断开状态，而不是立即删除
                    std::lock_guard<std::mutex> disconnectLock(disconnectedClientsMutex);
                    disconnectedClients.push_back(client);
                    ++it;
                }
            }
        }
        
        return success;
    }
    
    // 广播二进制数据给所有客户端
    bool broadcastBinary(const std::vector<float>& data, const std::string& targetClientId = "") {
        cleanupDisconnectedClients();
        
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
        
        std::lock_guard<std::mutex> lock(clientsMutex);
        bool success = true;
        
        // 检查是否需要发送给特定客户端
        if (!targetClientId.empty()) {
            // 查找特定客户端
            auto it = std::find_if(clients.begin(), clients.end(), [&targetClientId](const std::shared_ptr<ClientConnection>& client) {
                return client->clientId == targetClientId && client->connected;
            });
            
            if (it != clients.end()) {
                // 发送给特定客户端
                auto client = *it;
                if (!sendFrame(client->socket, TEXT, (const uint8_t*)ss.str().c_str(), ss.str().length())) {
                    success = false;
                    client->connected = false;
                }
            } else {
                // 未找到指定客户端
                success = false;
            }
        } else {
            // 广播给所有客户端
            for (auto it = clients.begin(); it != clients.end();) {
                auto client = *it;
                if (client->connected) {
                    if (!sendFrame(client->socket, TEXT, (const uint8_t*)ss.str().c_str(), ss.str().length())) {
                        success = false;
                        client->connected = false;
                        // 安全地标记客户端为断开状态，而不是立即删除
                        std::lock_guard<std::mutex> disconnectLock(disconnectedClientsMutex);
                        disconnectedClients.push_back(client);
                        ++it;
                    } else {
                        ++it;
                    }
                } else {
                    // 安全地标记客户端为断开状态，而不是立即删除
                    std::lock_guard<std::mutex> disconnectLock(disconnectedClientsMutex);
                    disconnectedClients.push_back(client);
                    ++it;
                }
            }
        }
        
        return success;
    }
    
    // 设置消息接收回调
    void setReceiveCallback(std::function<void(const std::string&, const std::string&)> callback) {
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
            socklen_t addrLen = sizeof(clientAddr);
            socket_t clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &addrLen);
            
            if (clientSocket == INVALID_SOCKET_VALUE) {
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
                    std::cout << "已添加客户端: " << client->clientId << "，当前连接数: " << clients.size() << std::endl;
                }
                
                // 启动接收线程
                client->receiveThread = new std::thread(&WebSocketImpl::receiveLoop, this, client);
            } else {
                CLOSE_SOCKET(clientSocket);
            }
        }
    }
    
    // 处理WebSocket握手
    bool handleHandshake(socket_t clientSocket) {
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
        if (send(clientSocket, response.c_str(), response.length(), 0) == SOCKET_ERROR_VALUE) {
            return false;
        }
        
        return true;
    }
    
    // 计算WebSocket握手的Accept Key
    std::string computeAcceptKey(const std::string& key) {
        // 标准WebSocket协议实现：SHA1+Base64
        // 添加WebSocket协议指定的魔术字符串
        std::string combined = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        
        // SHA1实现
        // 这是一个极简的SHA1实现，仅用于WebSocket握手
        unsigned int H0 = 0x67452301;
        unsigned int H1 = 0xEFCDAB89;
        unsigned int H2 = 0x98BADCFE;
        unsigned int H3 = 0x10325476;
        unsigned int H4 = 0xC3D2E1F0;
        
        // 预处理消息
        std::vector<unsigned char> msg(combined.begin(), combined.end());
        size_t initial_length = msg.size();
        
        // 添加1位
        msg.push_back(0x80);
        
        // 填充0直到长度是64的倍数减8
        while (msg.size() % 64 != 56) {
            msg.push_back(0);
        }
        
        // 添加64位长度
        uint64_t bit_length = initial_length * 8;
        for (int i = 7; i >= 0; --i) {
            msg.push_back((bit_length >> (i * 8)) & 0xFF);
        }
        
        // 处理消息块
        for (size_t i = 0; i < msg.size(); i += 64) {
            uint32_t w[80];
            
            // 将块分成16个32位字
            for (int j = 0; j < 16; ++j) {
                w[j] = (msg[i + j * 4] << 24) |
                      (msg[i + j * 4 + 1] << 16) |
                      (msg[i + j * 4 + 2] << 8) |
                      (msg[i + j * 4 + 3]);
            }
            
            // 扩展16个字到80个字
            for (int j = 16; j < 80; ++j) {
                w[j] = (w[j-3] ^ w[j-8] ^ w[j-14] ^ w[j-16]);
                w[j] = (w[j] << 1) | (w[j] >> 31);
            }
            
            // 初始化哈希值
            uint32_t a = H0;
            uint32_t b = H1;
            uint32_t c = H2;
            uint32_t d = H3;
            uint32_t e = H4;
            
            // 主循环
            for (int j = 0; j < 80; ++j) {
                uint32_t f, k;
                
                if (j < 20) {
                    f = (b & c) | ((~b) & d);
                    k = 0x5A827999;
                } else if (j < 40) {
                    f = b ^ c ^ d;
                    k = 0x6ED9EBA1;
                } else if (j < 60) {
                    f = (b & c) | (b & d) | (c & d);
                    k = 0x8F1BBCDC;
                } else {
                    f = b ^ c ^ d;
                    k = 0xCA62C1D6;
                }
                
                uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[j];
                e = d;
                d = c;
                c = (b << 30) | (b >> 2);
                b = a;
                a = temp;
            }
            
            // 添加这个块的哈希到结果
            H0 += a;
            H1 += b;
            H2 += c;
            H3 += d;
            H4 += e;
        }
        
        // 产生最终的哈希值
        unsigned char hash[20];
        for (int i = 0; i < 4; ++i) {
            hash[i]      = (H0 >> (24 - i * 8)) & 0xFF;
            hash[i + 4]  = (H1 >> (24 - i * 8)) & 0xFF;
            hash[i + 8]  = (H2 >> (24 - i * 8)) & 0xFF;
            hash[i + 12] = (H3 >> (24 - i * 8)) & 0xFF;
            hash[i + 16] = (H4 >> (24 - i * 8)) & 0xFF;
        }
        
        // Base64编码结果
        const char* base64_chars = 
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        
        std::string result;
        unsigned int i = 0;
        unsigned int j = 0;
        unsigned char char_array_3[3];
        unsigned char char_array_4[4];
        
        for (i = 0; i < 20; i++) {
            char_array_3[j++] = hash[i];
            if (j == 3) {
                char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
                char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
                char_array_4[3] = char_array_3[2] & 0x3f;
                
                for (int k = 0; k < 4; k++)
                    result += base64_chars[char_array_4[k]];
                j = 0;
            }
        }
        
        if (j) {
            for (int k = j; k < 3; k++)
                char_array_3[k] = '\0';
            
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            
            for (int k = 0; k < j + 1; k++)
                result += base64_chars[char_array_4[k]];
            
            while (j++ < 3)
                result += '=';
        }
        
        return result;
    }
    
    // 接收客户端数据的线程函数
    void receiveLoop(std::shared_ptr<ClientConnection> client) {
        // 设置socket为非阻塞模式，以便能够检测断开连接
#ifdef _WIN32
        u_long mode = 1;  // 非阻塞模式
        ioctlsocket(client->socket, FIONBIO, &mode);
#else
        int flags = fcntl(client->socket, F_GETFL, 0);
        fcntl(client->socket, F_SETFL, flags | O_NONBLOCK);
#endif

        while (client->connected && running) {
            // 接收帧头
            uint8_t frameHeader[2] = {0};
            int recvResult = recv(client->socket, (char*)frameHeader, 2, 0);
            
            if (recvResult <= 0) {
                // 检查是否是非阻塞模式下暂时没有数据
                if (recvResult == SOCKET_ERROR_VALUE && SOCKET_LAST_ERROR == SOCKET_EWOULDBLOCK) {
                    // 暂时没有数据，等待一会再试
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }
                
                // 连接已断开
                std::cout << "检测到客户端连接断开: " << client->clientId << std::endl;
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
                        receiveCallback(message, client->clientId);
                    }
                    break;
                }
                
                case BINARY: {
                    // 将二进制数据转换为float数组
                    std::vector<float> audio_data(payloadLength / sizeof(float));
                    memcpy(audio_data.data(), payload.data(), payloadLength);
                    
                    // 进行声纹识别
                    std::string speaker = VoiceprintRecognition::getInstance().processAudio(audio_data, 16000.0f);
                    
                    // 更新客户端说话人信息
                    if (speaker != "unknown" && speaker != client->current_speaker) {
                        client->current_speaker = speaker;
                        
                        // 发送说话人信息给客户端
                        std::string speaker_info = "{\"type\":\"speaker\",\"speaker\":\"" + speaker + "\"}";
                        sendFrame(client->socket, TEXT, (const uint8_t*)speaker_info.c_str(), speaker_info.length());
                    }
                    
                    // 继续处理音频数据
                    // 将二进制数据转换为字符串
                    std::string message((char*)payload.data(), payload.size());
                    
                    // 调用回调
                    std::lock_guard<std::mutex> lock(callbackMutex);
                    if (receiveCallback) {
                        receiveCallback(message, client->clientId);
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
                    // std::cout << "客户端断开连接: " << client->clientId << "，当前连接数: " << clients.size() << std::endl;
                    
                    // 关闭socket
                    if (client->socket != INVALID_SOCKET_VALUE) {
                        CLOSE_SOCKET(client->socket);
                        client->socket = INVALID_SOCKET_VALUE;
                    }
                    break;
                }
                
                default:
                    std::cout << "未知..." << std::endl;
                    break;
            }
        }
        
        client->connected = false;
        
        // 处理客户端断开连接的情况
        std::cout << "客户端已断开连接: " << client->clientId << std::endl;
        
        // 不直接从列表中移除，而是标记为断开状态
        {
            std::lock_guard<std::mutex> lock(disconnectedClientsMutex);
            disconnectedClients.push_back(client);
        }
    }
    
    // 发送WebSocket帧
    bool sendFrame(socket_t socket, OpCode opcode, const uint8_t* payload, size_t length) {
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
        return result != SOCKET_ERROR_VALUE;
    }
    
    // 周期性清理已断开连接的客户端
    void cleanupLoop() {
        while (cleanupThreadRunning) {
            // 每隔一段时间清理一次断开的客户端
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            cleanupDisconnectedClients();
        }
    }
    
    // 清理已断开连接的客户端
    void cleanupDisconnectedClients() {
        std::vector<std::shared_ptr<ClientConnection>> clientsToRemove;
        
        // 先获取需要删除的客户端列表
        {
            std::lock_guard<std::mutex> lock(disconnectedClientsMutex);
            if (disconnectedClients.empty()) {
                return;
            }
            
            clientsToRemove.swap(disconnectedClients);
        }
        
        // 然后从客户端列表中移除
        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            for (auto& clientToRemove : clientsToRemove) {
                // 关闭socket
                if (clientToRemove->socket != INVALID_SOCKET_VALUE) {
                    CLOSE_SOCKET(clientToRemove->socket);
                    clientToRemove->socket = INVALID_SOCKET_VALUE;
                }
                
                // 从列表中删除
                auto it = std::find_if(clients.begin(), clients.end(), 
                    [&clientToRemove](const std::shared_ptr<ClientConnection>& c) {
                        return c->clientId == clientToRemove->clientId;
                    });
                
                if (it != clients.end()) {
                    std::cout << "清理已断开的客户端: " << (*it)->clientId << std::endl;
                    clients.erase(it);
                    std::cout << "当前连接数: " << clients.size() << std::endl;
                }
            }
        }
    }
    
    socket_t serverSocket;
    std::atomic<bool> running;
    std::thread* acceptThread;
    std::thread* cleanupThread;
    std::atomic<bool> cleanupThreadRunning;
    std::vector<std::shared_ptr<ClientConnection>> clients;
    std::mutex clientsMutex;
    std::vector<std::shared_ptr<ClientConnection>> disconnectedClients;
    std::mutex disconnectedClientsMutex;
    std::function<void(const std::string&, const std::string&)> receiveCallback;
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

bool WebSocketServer::broadcastText(const std::string& message, const std::string& targetClientId) {
    if (!impl_ || !running_) {
        return false;
    }
    return impl_->broadcastText(message, targetClientId);
}

bool WebSocketServer::broadcastBinary(const std::vector<float>& data, const std::string& targetClientId) {
    if (!impl_ || !running_) {
        return false;
    }
    return impl_->broadcastBinary(data, targetClientId);
}

void WebSocketServer::setReceiveCallback(std::function<void(const std::string&, const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    receiveCallback_ = callback;
    
    if (impl_) {
        impl_->setReceiveCallback([this](const std::string& message, const std::string& clientId) {
            std::lock_guard<std::mutex> lock(callbackMutex_);
            if (receiveCallback_) {
                receiveCallback_(message, clientId);
            }
        });
    }
}

bool WebSocketServer::isRunning() const {
    return running_ && impl_ && impl_->isRunning();
} 