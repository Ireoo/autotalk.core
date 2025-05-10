#pragma once

#include <vector>
#include <functional>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <memory>
#include <map>

class WebSocketServer;

// 音频数据结构
struct AudioData {
    std::vector<float> buffer;
    std::string clientId;
};

class AudioServer {
public:
    AudioServer();
    ~AudioServer();

    // 初始化websocket服务器
    bool initialize(const std::string& host = "localhost", int port = 3000);
    
    // 开始监听音频数据
    bool start(std::function<void(const std::vector<float>&, const std::string&)> callback);
    
    // 停止监听
    void stop();
    
    // 发送处理后的音频数据
    void sendAudioData(const std::vector<float>& audioData, const std::string& targetClientId = "");
    
    // 发送文本识别结果
    void sendTextResult(const std::string& text, bool isComplete, const std::string& targetClientId = "");

private:
    // WebSocket服务器
    std::shared_ptr<WebSocketServer> server_;
    
    // 回调函数
    std::function<void(const std::vector<float>&, const std::string&)> audioCallback_;
    
    // 线程安全队列，用于存储接收到的音频数据
    std::queue<AudioData> audioQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCondition_;
    
    // 处理线程
    std::thread processingThread_;
    
    // 运行标志
    std::atomic<bool> running_;
    
    // 连接状态
    std::atomic<bool> connected_;
    
    // 服务器信息
    std::string host_;
    int port_;
    
    // 处理音频数据的线程函数
    void processAudioData();
    
    // 接收数据处理函数
    void handleIncomingMessage(const std::string& message, const std::string& clientId);
}; 