#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <deque>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <pdh.h>
#endif

// 音频信号数据
struct AudioSignalData {
    std::deque<float> levels;  // 音频电平历史
    float currentLevel;        // 当前电平
    std::mutex mutex;
    int maxSamples;            // 保存的最大样本数

    AudioSignalData(int maxSamples = 100) : maxSamples(maxSamples), currentLevel(0.0f) {}
    
    // 删除复制构造函数和复制赋值运算符
    AudioSignalData(const AudioSignalData&) = delete;
    AudioSignalData& operator=(const AudioSignalData&) = delete;
    
    // 添加移动构造函数和移动赋值运算符
    AudioSignalData(AudioSignalData&& other) noexcept
        : levels(std::move(other.levels)),
          currentLevel(other.currentLevel),
          maxSamples(other.maxSamples) {}
          
    AudioSignalData& operator=(AudioSignalData&& other) noexcept {
        if (this != &other) {
            levels = std::move(other.levels);
            currentLevel = other.currentLevel;
            maxSamples = other.maxSamples;
        }
        return *this;
    }
};

// CPU使用率数据
struct CPUUsageData {
    std::deque<float> usageHistory;  // CPU使用率历史
    float currentUsage;              // 当前使用率
    std::mutex mutex;
    int maxSamples;                  // 保存的最大样本数

    CPUUsageData(int maxSamples = 100) : maxSamples(maxSamples), currentUsage(0.0f) {}
    
    // 删除复制构造函数和复制赋值运算符
    CPUUsageData(const CPUUsageData&) = delete;
    CPUUsageData& operator=(const CPUUsageData&) = delete;
    
    // 添加移动构造函数和移动赋值运算符
    CPUUsageData(CPUUsageData&& other) noexcept
        : usageHistory(std::move(other.usageHistory)),
          currentUsage(other.currentUsage),
          maxSamples(other.maxSamples) {}
          
    CPUUsageData& operator=(CPUUsageData&& other) noexcept {
        if (this != &other) {
            usageHistory = std::move(other.usageHistory);
            currentUsage = other.currentUsage;
            maxSamples = other.maxSamples;
        }
        return *this;
    }
};

// GPU使用率数据
struct GPUUsageData {
    std::deque<float> usageHistory;  // GPU使用率历史
    float currentUsage;              // 当前使用率
    std::mutex mutex;
    int maxSamples;                  // 保存的最大样本数
    bool available;                  // GPU监控是否可用

    GPUUsageData(int maxSamples = 100) : maxSamples(maxSamples), currentUsage(0.0f), available(false) {}
    
    // 删除复制构造函数和复制赋值运算符
    GPUUsageData(const GPUUsageData&) = delete;
    GPUUsageData& operator=(const GPUUsageData&) = delete;
    
    // 添加移动构造函数和移动赋值运算符
    GPUUsageData(GPUUsageData&& other) noexcept
        : usageHistory(std::move(other.usageHistory)),
          currentUsage(other.currentUsage),
          maxSamples(other.maxSamples),
          available(other.available) {}
          
    GPUUsageData& operator=(GPUUsageData&& other) noexcept {
        if (this != &other) {
            usageHistory = std::move(other.usageHistory);
            currentUsage = other.currentUsage;
            maxSamples = other.maxSamples;
            available = other.available;
        }
        return *this;
    }
};

class SystemMonitor {
public:
    SystemMonitor();
    ~SystemMonitor();

    bool start();
    void stop();
    float getCpuUsage() const;
    float getMemoryUsage() const;

    // 初始化监控系统
    bool initialize();

    // 更新音频信号数据
    void updateAudioSignal(const std::vector<float>& audioData);

    // 获取音频信号数据（用于绘制）
    AudioSignalData getAudioSignalData();

    // 更新CPU使用率
    void updateCPUUsage();

    // 获取CPU使用率数据（用于绘制）
    CPUUsageData getCPUUsageData();

    // 更新GPU使用率（如果可用）
    void updateGPUUsage();

    // 获取GPU使用率数据（用于绘制）
    GPUUsageData getGPUUsageData();

    // 启动监控线程
    void startMonitoring();

    // 停止监控线程
    void stopMonitoring();

private:
    void monitorThread();
    float calculateCpuUsage();
    float calculateMemoryUsage();

    AudioSignalData audioSignalData_;
    CPUUsageData cpuUsageData_;
    GPUUsageData gpuUsageData_;

    std::atomic<bool> running_;
    std::mutex mutex_;
    std::atomic<float> cpuUsage_;
    std::atomic<float> memoryUsage_;
    std::thread monitorThread_;

#ifdef _WIN32
    // Windows性能计数器
    PDH_HQUERY cpuQuery_;
    PDH_HCOUNTER cpuTotal_;
    
    // GPU查询相关变量
    PDH_HQUERY gpuQuery_;
    PDH_HCOUNTER gpuCounter_;
#endif
}; 