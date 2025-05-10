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
#elif defined(__APPLE__) || defined(__MACH__)
// macOS特有的头文件
#include <unistd.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <mach/mach_host.h>
#else
// Linux特有的头文件
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
    // Windows平台初始化代码（保持原样）
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
#elif defined(__APPLE__) || defined(__MACH__)
    // macOS平台初始化代码
    // 无需特殊初始化
#else
    // Linux平台初始化代码
    // 无需特殊初始化
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
#elif defined(__APPLE__) || defined(__MACH__)
    // macOS实现CPU使用率计算
    // 注意：此处简化实现，仅返回系统总体CPU使用率
    // 完整实现应使用mach_host_processor_info等API获取更详细信息
    
    static processor_info_array_t prevCpuInfo = nullptr;
    static mach_msg_type_number_t prevCpuInfoCnt = 0;
    static uint64_t prevTotalTicks = 0;
    static uint64_t prevIdleTicks = 0;

    natural_t numCPUs = 0;
    processor_info_array_t cpuInfo;
    mach_msg_type_number_t numCpuInfo = 0;

    host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &numCPUs, &cpuInfo, &numCpuInfo);
    
    uint64_t totalTicks = 0;
    uint64_t idleTicks = 0;

    for (unsigned int i = 0; i < numCPUs; i++) {
        processor_cpu_load_info_data_t* cpuLoad = (processor_cpu_load_info_data_t*)(cpuInfo + (i * CPU_STATE_MAX));
        totalTicks += cpuLoad->cpu_ticks[CPU_STATE_USER] + cpuLoad->cpu_ticks[CPU_STATE_SYSTEM] + 
                     cpuLoad->cpu_ticks[CPU_STATE_IDLE] + cpuLoad->cpu_ticks[CPU_STATE_NICE];
        idleTicks += cpuLoad->cpu_ticks[CPU_STATE_IDLE];
    }

    float usage = 0.0f;
    
    if (prevCpuInfo) {
        uint64_t totalTicksSinceLastTime = totalTicks - prevTotalTicks;
        uint64_t idleTicksSinceLastTime = idleTicks - prevIdleTicks;
        
        if (totalTicksSinceLastTime > 0) {
            usage = 100.0f * (1.0f - ((float)idleTicksSinceLastTime) / (float)totalTicksSinceLastTime);
        }
        
        // 释放上一次分配的内存
        vm_deallocate(mach_task_self(), (vm_address_t)prevCpuInfo, sizeof(processor_cpu_load_info_data_t) * prevCpuInfoCnt);
    }

    // 保存当前状态以供下次计算使用
    prevCpuInfo = cpuInfo;
    prevCpuInfoCnt = numCpuInfo;
    prevTotalTicks = totalTicks;
    prevIdleTicks = idleTicks;
    
    return usage;
#else
    // Linux平台实现或其他平台的默认实现
    return 0.0f;
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
#elif defined(__APPLE__) || defined(__MACH__)
    // macOS实现内存使用率计算
    // 获取进程内存信息
    struct task_basic_info t_info;
    mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;
    
    if (task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&t_info, &t_info_count) != KERN_SUCCESS) {
        return 0.0f;
    }
    
    // 获取系统总内存
    int64_t physical_memory = 0;
    size_t length = sizeof(int64_t);
    sysctlbyname("hw.memsize", &physical_memory, &length, NULL, 0);
    
    if (physical_memory > 0) {
        return static_cast<float>(t_info.resident_size) / static_cast<float>(physical_memory) * 100.0f;
    }
    
    return 0.0f;
#else
    // Linux平台实现或其他平台的默认实现
    return 0.0f;
#endif
} 