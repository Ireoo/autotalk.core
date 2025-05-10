#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>

class VoiceprintRecognition {
public:
    static VoiceprintRecognition& getInstance();
    
    // 初始化声纹识别模型
    bool initialize(const std::string& model_path);
    
    // 处理音频数据并返回说话人信息
    std::string processAudio(const std::vector<float>& audio_data, float sample_rate);
    
    // 获取当前说话人ID
    std::string getCurrentSpeaker() const;
    
    // 设置说话人阈值
    void setSpeakerThreshold(float threshold);
    
private:
    VoiceprintRecognition() = default;
    ~VoiceprintRecognition() = default;
    
    // 禁止拷贝和赋值
    VoiceprintRecognition(const VoiceprintRecognition&) = delete;
    VoiceprintRecognition& operator=(const VoiceprintRecognition&) = delete;
    
    // 声纹识别模型相关变量
    void* model_handle_ = nullptr;
    std::string current_speaker_;
    float speaker_threshold_ = 0.7f;
    mutable std::mutex mutex_;
    
    // 内部处理函数
    bool loadModel(const std::string& model_path);
    std::string extractFeatures(const std::vector<float>& audio_data);
    std::string identifySpeaker(const std::string& features);
}; 