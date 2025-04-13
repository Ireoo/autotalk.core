#include "../include/system_monitor.h"
#include <thread>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <iostream>

#ifdef _WIN32
#pragma comment(lib, "pdh.lib")
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#include <sys/sysinfo.h>
#endif

SystemMonitor::SystemMonitor() 
    : running_(false)
    , cpuUsage_(0.0f)
    , memoryUsage_(0.0f) {
}

SystemMonitor::~SystemMonitor() {
    stop();
    
#ifdef _WIN32
    if (cpuQuery_) {
        PdhCloseQuery(cpuQuery_);
    }
    if (gpuQuery_) {
        PdhCloseQuery(gpuQuery_);
    }
#endif
}

bool SystemMonitor::initialize() {
#ifdef _WIN32
    // 初始化CPU性能计数器
    PDH_STATUS status = PdhOpenQuery(NULL, 0, &cpuQuery_);
    if (status != ERROR_SUCCESS) {
        std::cerr << "无法打开PDH查询，错误码: " << status << std::endl;
        return false;
    }

    // 添加CPU使用率计数器
    status = PdhAddCounterA(cpuQuery_, "\\Processor(_Total)\\% Processor Time", 0, &cpuTotal_);
    if (status != ERROR_SUCCESS) {
        std::cerr << "无法添加CPU计数器，错误码: " << status << std::endl;
        PdhCloseQuery(cpuQuery_);
        return false;
    }

    // 第一次收集数据，初始化计数器
    status = PdhCollectQueryData(cpuQuery_);
    if (status != ERROR_SUCCESS) {
        std::cerr << "无法收集查询数据，错误码: " << status << std::endl;
        PdhCloseQuery(cpuQuery_);
        return false;
    }

    // 尝试初始化GPU计数器 (如果可用)
    gpuUsageData_.available = false;
    status = PdhOpenQuery(NULL, 0, &gpuQuery_);
    if (status == ERROR_SUCCESS) {
        // 尝试添加GPU使用率计数器 (NVIDIA)
        status = PdhAddCounterA(gpuQuery_, "\\GPU Engine(*)\\Utilization Percentage", 0, &gpuCounter_);
        if (status == ERROR_SUCCESS) {
            gpuUsageData_.available = true;
            PdhCollectQueryData(gpuQuery_);
        } else {
            // 尝试AMD的计数器或其他计数器
            // ...
            PdhCloseQuery(gpuQuery_);
        }
    }
#endif

    return true;
}

void SystemMonitor::updateAudioSignal(const std::vector<float>& audioData) {
    if (audioData.empty()) {
        return;
    }

    // 计算当前音频样本的平均振幅
    float sum = 0.0f;
    for (const auto& sample : audioData) {
        sum += std::abs(sample);
    }
    float avgAmplitude = sum / audioData.size();

    // 更新音频信号数据
    {
        std::lock_guard<std::mutex> lock(audioSignalData_.mutex);
        audioSignalData_.currentLevel = avgAmplitude;
        audioSignalData_.levels.push_back(avgAmplitude);
        
        // 保持历史记录不超过最大样本数
        while (audioSignalData_.levels.size() > audioSignalData_.maxSamples) {
            audioSignalData_.levels.pop_front();
        }
    }
}

AudioSignalData SystemMonitor::getAudioSignalData() {
    AudioSignalData result;
    {
        std::lock_guard<std::mutex> lock(audioSignalData_.mutex);
        result.levels = audioSignalData_.levels;
        result.currentLevel = audioSignalData_.currentLevel;
        result.maxSamples = audioSignalData_.maxSamples;
    }
    return result;
}

void SystemMonitor::updateCPUUsage() {
#ifdef _WIN32
    PDH_FMT_COUNTERVALUE counterVal;
    
    // 收集当前的CPU使用数据
    PDH_STATUS status = PdhCollectQueryData(cpuQuery_);
    if (status != ERROR_SUCCESS) {
        return;
    }
    
    // 格式化结果
    status = PdhGetFormattedCounterValue(cpuTotal_, PDH_FMT_DOUBLE, NULL, &counterVal);
    if (status != ERROR_SUCCESS) {
        return;
    }
    
    // 更新CPU使用率数据
    {
        std::lock_guard<std::mutex> lock(cpuUsageData_.mutex);
        cpuUsageData_.currentUsage = static_cast<float>(counterVal.doubleValue);
        cpuUsageData_.usageHistory.push_back(cpuUsageData_.currentUsage);
        
        // 保持历史记录不超过最大样本数
        while (cpuUsageData_.usageHistory.size() > cpuUsageData_.maxSamples) {
            cpuUsageData_.usageHistory.pop_front();
        }
    }
#endif
}

CPUUsageData SystemMonitor::getCPUUsageData() {
    CPUUsageData result;
    {
        std::lock_guard<std::mutex> lock(cpuUsageData_.mutex);
        result.usageHistory = cpuUsageData_.usageHistory;
        result.currentUsage = cpuUsageData_.currentUsage;
        result.maxSamples = cpuUsageData_.maxSamples;
    }
    return result;
}

void SystemMonitor::updateGPUUsage() {
#ifdef _WIN32
    if (!gpuUsageData_.available) {
        return;
    }
    
    PDH_FMT_COUNTERVALUE counterVal;
    
    // 收集当前的GPU使用数据
    PDH_STATUS status = PdhCollectQueryData(gpuQuery_);
    if (status != ERROR_SUCCESS) {
        return;
    }
    
    // 格式化结果
    status = PdhGetFormattedCounterValue(gpuCounter_, PDH_FMT_DOUBLE, NULL, &counterVal);
    if (status != ERROR_SUCCESS) {
        return;
    }
    
    // 更新GPU使用率数据
    {
        std::lock_guard<std::mutex> lock(gpuUsageData_.mutex);
        gpuUsageData_.currentUsage = static_cast<float>(counterVal.doubleValue);
        gpuUsageData_.usageHistory.push_back(gpuUsageData_.currentUsage);
        
        // 保持历史记录不超过最大样本数
        while (gpuUsageData_.usageHistory.size() > gpuUsageData_.maxSamples) {
            gpuUsageData_.usageHistory.pop_front();
        }
    }
#endif
}

GPUUsageData SystemMonitor::getGPUUsageData() {
    GPUUsageData result;
    {
        std::lock_guard<std::mutex> lock(gpuUsageData_.mutex);
        result.usageHistory = gpuUsageData_.usageHistory;
        result.currentUsage = gpuUsageData_.currentUsage;
        result.maxSamples = gpuUsageData_.maxSamples;
        result.available = gpuUsageData_.available;
    }
    return result;
}

bool SystemMonitor::start() {
    if (running_) {
        return true;
    }

    running_ = true;
    monitorThread_ = std::thread(&SystemMonitor::monitorThread, this);
    return true;
}

void SystemMonitor::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }
}

float SystemMonitor::getCpuUsage() const {
    return cpuUsage_;
}

float SystemMonitor::getMemoryUsage() const {
    return memoryUsage_;
}

void SystemMonitor::monitorThread() {
    while (running_) {
        cpuUsage_ = calculateCpuUsage();
        memoryUsage_ = calculateMemoryUsage();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

float SystemMonitor::calculateCpuUsage() {
#ifdef _WIN32
    static ULARGE_INTEGER lastCPU = {0};
    static ULARGE_INTEGER lastSysCPU = {0};
    static ULARGE_INTEGER lastUserCPU = {0};
    static int numProcessors = 0;
    static HANDLE self = GetCurrentProcess();

    if (numProcessors == 0) {
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        numProcessors = sysInfo.dwNumberOfProcessors;
    }

    FILETIME ftime, fsys, fuser;
    ULARGE_INTEGER now, sys, user;

    GetSystemTimeAsFileTime(&ftime);
    memcpy(&now, &ftime, sizeof(FILETIME));

    GetProcessTimes(self, &ftime, &ftime, &fsys, &fuser);
    memcpy(&sys, &fsys, sizeof(FILETIME));
    memcpy(&user, &fuser, sizeof(FILETIME));

    if (lastCPU.QuadPart == 0) {
        lastCPU = now;
        lastSysCPU = sys;
        lastUserCPU = user;
        return 0.0f;
    }

    float percent = static_cast<float>((sys.QuadPart - lastSysCPU.QuadPart) +
        (user.QuadPart - lastUserCPU.QuadPart));
    percent /= (now.QuadPart - lastCPU.QuadPart);
    percent /= numProcessors;
    percent *= 100.0f;

    lastCPU = now;
    lastSysCPU = sys;
    lastUserCPU = user;

    return percent;
#else
    return 0.0f; // 在非 Windows 系统上暂时返回 0
#endif
}

float SystemMonitor::calculateMemoryUsage() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memInfo)) {
            return static_cast<float>(pmc.WorkingSetSize) / static_cast<float>(memInfo.ullTotalPhys) * 100.0f;
        }
    }
    return 0.0f;
#else
    return 0.0f; // 在非 Windows 系统上暂时返回 0
#endif
} 