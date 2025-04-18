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
    int numCores;                    // CPU核心数

    CPUUsageData(int maxSamples = 100) : maxSamples(maxSamples), currentUsage(0.0f), numCores(0) {}
    
    // 删除复制构造函数和复制赋值运算符
    CPUUsageData(const CPUUsageData&) = delete;
    CPUUsageData& operator=(const CPUUsageData&) = delete;
    
    // 添加移动构造函数和移动赋值运算符
    CPUUsageData(CPUUsageData&& other) noexcept
        : usageHistory(std::move(other.usageHistory)),
          currentUsage(other.currentUsage),
          maxSamples(other.maxSamples),
          numCores(other.numCores) {}
          
    CPUUsageData& operator=(CPUUsageData&& other) noexcept {
        if (this != &other) {
            usageHistory = std::move(other.usageHistory);
            currentUsage = other.currentUsage;
            maxSamples = other.maxSamples;
            numCores = other.numCores;
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
    
    // 新增GPU监控数据
    float memoryUsageMB;             // GPU内存使用量(MB)
    float memoryTotalMB;             // GPU总内存(MB)
    float memoryUsagePercent;        // GPU内存使用百分比
    float temperature;               // GPU温度(摄氏度)
    float power;                     // GPU当前功耗(瓦特)
    std::string gpuName;             // GPU名称
    std::string driverVersion;       // 驱动版本
    int gpuIndex;                    // GPU索引

    GPUUsageData(int maxSamples = 100) : 
        maxSamples(maxSamples), 
        currentUsage(0.0f), 
        available(false),
        memoryUsageMB(0.0f),
        memoryTotalMB(0.0f),
        memoryUsagePercent(0.0f),
        temperature(0.0f),
        power(0.0f),
        gpuName("未知"),
        driverVersion("未知"),
        gpuIndex(0) {}
    
    // 删除复制构造函数和复制赋值运算符
    GPUUsageData(const GPUUsageData&) = delete;
    GPUUsageData& operator=(const GPUUsageData&) = delete;
    
    // 添加移动构造函数和移动赋值运算符
    GPUUsageData(GPUUsageData&& other) noexcept
        : usageHistory(std::move(other.usageHistory)),
          currentUsage(other.currentUsage),
          maxSamples(other.maxSamples),
          available(other.available),
          memoryUsageMB(other.memoryUsageMB),
          memoryTotalMB(other.memoryTotalMB),
          memoryUsagePercent(other.memoryUsagePercent),
          temperature(other.temperature),
          power(other.power),
          gpuName(std::move(other.gpuName)),
          driverVersion(std::move(other.driverVersion)),
          gpuIndex(other.gpuIndex) {}
          
    GPUUsageData& operator=(GPUUsageData&& other) noexcept {
        if (this != &other) {
            usageHistory = std::move(other.usageHistory);
            currentUsage = other.currentUsage;
            maxSamples = other.maxSamples;
            available = other.available;
            memoryUsageMB = other.memoryUsageMB;
            memoryTotalMB = other.memoryTotalMB;
            memoryUsagePercent = other.memoryUsagePercent;
            temperature = other.temperature;
            power = other.power;
            gpuName = std::move(other.gpuName);
            driverVersion = std::move(other.driverVersion);
            gpuIndex = other.gpuIndex;
        }
        return *this;
    }
    
    // 不带mutex的数据拷贝方法
    void copyDataFrom(const GPUUsageData& other) {
        usageHistory = other.usageHistory;
        currentUsage = other.currentUsage;
        maxSamples = other.maxSamples;
        available = other.available;
        memoryUsageMB = other.memoryUsageMB;
        memoryTotalMB = other.memoryTotalMB;
        memoryUsagePercent = other.memoryUsagePercent;
        temperature = other.temperature;
        power = other.power;
        gpuName = other.gpuName;
        driverVersion = other.driverVersion;
        gpuIndex = other.gpuIndex;
    }
    
    // 创建不包含互斥锁的副本
    GPUUsageData createCopy() const {
        GPUUsageData copy;
        copy.usageHistory = this->usageHistory;
        copy.currentUsage = this->currentUsage;
        copy.maxSamples = this->maxSamples;
        copy.available = this->available;
        copy.memoryUsageMB = this->memoryUsageMB;
        copy.memoryTotalMB = this->memoryTotalMB;
        copy.memoryUsagePercent = this->memoryUsagePercent;
        copy.temperature = this->temperature;
        copy.power = this->power;
        copy.gpuName = this->gpuName;
        copy.driverVersion = this->driverVersion;
        copy.gpuIndex = this->gpuIndex;
        return copy;
    }
};

// 多GPU信息结构体
struct MultiGPUInfo {
    std::vector<GPUUsageData> gpus;
    std::mutex mutex;
    int activeGPU; // Whisper正在使用的GPU索引
    
    MultiGPUInfo() : activeGPU(-1) {}
};

class SystemMonitor {
public:
    SystemMonitor();
    ~SystemMonitor();

    bool start();
    void stop();
    float getCpuUsage() const;
    float getMemoryUsage() const;
    float getMemoryUsageMB() const; // 获取内存使用量（MB）
    
    // 添加GPU相关的getter方法
    bool isGPUAvailable() const;    // 判断GPU是否可用
    float getGPUUsage() const;      // 获取GPU使用率
    float getGPUMemoryUsageMB() const;   // 获取GPU内存使用量(MB)
    float getGPUMemoryTotalMB() const;   // 获取GPU总内存(MB)
    float getGPUMemoryPercent() const;   // 获取GPU内存使用百分比
    float getGPUTemperature() const;     // 获取GPU温度
    float getGPUPower() const;           // 获取GPU当前功耗
    std::string getGPUName() const;      // 获取GPU名称
    std::string getGPUDriverVersion() const; // 获取GPU驱动版本

    // 添加多GPU相关的getter方法
    int getGPUCount() const;                      // 获取GPU数量
    std::vector<GPUUsageData> getAllGPUs() const; // 获取所有GPU信息
    int getActiveGPU() const;                     // 获取Whisper正在使用的GPU索引
    void setActiveGPU(int index);                 // 设置Whisper正在使用的GPU索引

    // 初始化监控系统
    bool initialize();
    
    // 更新系统监控数据
    void update();

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

    // 添加获取CPU核心数的方法
    int getCPUCores() const;

private:
    void monitorThread();
    float calculateCpuUsage();
    float calculateMemoryUsage();
    float calculateMemoryUsageMB();

    AudioSignalData audioSignalData_;
    CPUUsageData cpuUsageData_;
    GPUUsageData gpuUsageData_;

    std::atomic<bool> running_;
    std::mutex mutex_;
    std::atomic<float> cpuUsage_;
    std::atomic<float> memoryUsage_;
    std::atomic<float> memoryUsageMB_;  // 内存使用量（MB）
    std::thread monitorThread_;

#ifdef _WIN32
    // Windows性能计数器
    PDH_HQUERY cpuQuery_;
    PDH_HCOUNTER cpuTotal_;
    
    // GPU查询相关变量
    PDH_HQUERY gpuQuery_;
    PDH_HCOUNTER gpuCounter_;
#endif

    MultiGPUInfo multiGPUInfo_; // 多GPU信息
}; 