#include "../include/audio_server.h"
#include "../include/websocket_client.h"
#include <iostream>
#include <functional>
#include <chrono>
#include <algorithm>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

AudioServer::AudioServer()
    : server_(nullptr), running_(false), connected_(false), host_("localhost"), port_(3000)
{
}

AudioServer::~AudioServer()
{
    stop();
}

bool AudioServer::initialize(const std::string &host, int port)
{
    host_ = host;
    port_ = port;

    // 创建WebSocket服务器
    server_ = std::make_shared<WebSocketServer>();

    // 设置消息接收回调
    server_->setReceiveCallback([this](const std::string &message, const std::string &clientId)
                                { handleIncomingMessage(message, clientId); });

    // 启动服务器
    if (!server_->start(port_))
    {
        std::cerr << "启动WebSocket服务器失败" << std::endl;
        return false;
    }

    connected_ = true;
    std::cout << "WebSocket服务器已启动，监听端口: " << port_ << std::endl;
    return true;
}

bool AudioServer::start(std::function<void(const std::vector<float> &, const std::string &)> callback)
{
    if (!connected_)
    {
        std::cerr << "服务器未启动，无法开始处理" << std::endl;
        return false;
    }

    audioCallback_ = callback;
    running_ = true;

    // 启动处理线程
    processingThread_ = std::thread(&AudioServer::processAudioData, this);

    return true;
}

void AudioServer::stop()
{
    running_ = false;

    // 通知处理线程停止
    queueCondition_.notify_all();

    // 等待处理线程结束
    if (processingThread_.joinable())
    {
        processingThread_.join();
    }

    // 停止WebSocket服务器
    if (server_)
    {
        server_->stop();
        server_.reset();
    }

    connected_ = false;
}

void AudioServer::sendAudioData(const std::vector<float> &audioData, const std::string &targetClientId)
{
    if (!connected_ || !server_)
    {
        return;
    }

    // 创建包含浮点数据的JSON对象
    json data_json = json::array();
    for (float sample : audioData)
    {
        data_json.push_back(sample);
    }

    // 构造消息
    json message = {
        {"type", "audio_response"},
        {"data", data_json}};

    // 发送JSON消息
    server_->broadcastText(message.dump(), targetClientId);
}

void AudioServer::sendTextResult(const std::string &text, bool isComplete, const std::string &targetClientId)
{
    if (!connected_ || !server_)
    {
        return;
    }

    try
    {
        // 添加适当的前缀
        std::string prefixedText = isComplete ? "T:" + text : "L:" + text;
        
        // 构造消息
        json message = {
            {"type", "text_result"},
            {"data", prefixedText}};

        // 发送JSON消息
        server_->broadcastText(message.dump(), targetClientId);
    }
    catch (const std::exception &e)
    {
        std::cerr << "发送JSON消息出错: " << e.what() << std::endl;
    }
}

void AudioServer::processAudioData()
{
    while (running_)
    {
        AudioData audioData;
        bool hasData = false;

        // 使用条件变量等待新的音频数据
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCondition_.wait(lock, [this]
                                 { return !audioQueue_.empty() || !running_; });

            // 检查是否需要退出
            if (!running_ && audioQueue_.empty())
            {
                break;
            }

            // 获取队列中的音频数据
            if (!audioQueue_.empty())
            {
                audioData = std::move(audioQueue_.front());
                audioQueue_.pop();
                hasData = true;
            }
        }

        // 处理音频数据
        if (hasData && audioCallback_)
        {
            audioCallback_(audioData.buffer, audioData.clientId);
        }
    }
}

void AudioServer::handleIncomingMessage(const std::string &message, const std::string &clientId)
{
    try
    {
        // 尝试解析JSON消息
        json json_msg = json::parse(message);

        // 获取消息类型
        std::string type = json_msg["type"];

        if (type == "audio_data")
        {
            // 从JSON解析音频数据
            std::vector<float> audioBuffer;

            // 获取数据数组
            json data_array = json_msg["data"];

            // 将JSON数组转换为浮点数数组
            for (const auto &item : data_array)
            {
                audioBuffer.push_back(item.get<float>());
            }

            // 将音频数据添加到队列
            if (!audioBuffer.empty())
            {
                AudioData data;
                data.buffer = std::move(audioBuffer);
                data.clientId = clientId;
                
                std::lock_guard<std::mutex> lock(queueMutex_);
                audioQueue_.push(std::move(data));
                queueCondition_.notify_one();
            }

            // std::cout << "收到音频数据，数据长度: " << data_array.size() << std::endl;
        }
    }
    catch (const json::exception &e)
    {
        // JSON解析失败，可能是普通文本消息或无效的UTF-8编码
        std::cout << "JSON解析错误: " << e.what() << std::endl;
        std::cout << "接收到的消息可能包含无效UTF-8字符" << std::endl;

        // 如果需要，可以回复一个简单的确认消息
        if (connected_ && server_)
        {
            json reply = {
                {"type", "error_response"},
                {"message", "消息解析失败，可能包含无效字符"}};
            server_->broadcastText(reply.dump(), clientId);
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "处理接收消息失败: " << e.what() << std::endl;
    }
}