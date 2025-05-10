#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>

// 前向声明
class WebSocketImpl;

class WebSocketServer {
public:
    WebSocketServer();
    ~WebSocketServer();

    // 启动WebSocket服务器
    bool start(int port = 3000);
    
    // 停止服务器
    void stop();
    
    // 发送文本消息给客户端
    bool broadcastText(const std::string& message, const std::string& targetClientId = "");
    
    // 发送二进制数据给客户端
    bool broadcastBinary(const std::vector<float>& data, const std::string& targetClientId = "");
    
    // 设置接收消息的回调
    void setReceiveCallback(std::function<void(const std::string&, const std::string&)> callback);
    
    // 检查是否正在运行
    bool isRunning() const;

private:
    // WebSocket实现
    std::unique_ptr<WebSocketImpl> impl_;
    
    // 是否正在运行
    std::atomic<bool> running_;
    
    // 接收消息的回调
    std::function<void(const std::string&, const std::string&)> receiveCallback_;
    
    // 回调互斥锁
    std::mutex callbackMutex_;
}; 