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

// 仅当有NVAPI SDK时才包含
#if defined(USE_CUDA) && defined(HAVE_NVAPI)
#define NVAPI_INTERNAL
#pragma comment(lib, "nvapi64.lib")
#include <nvapi.h>
#endif

#else
#include <unistd.h>
#include <sys/sysinfo.h>
#endif

SystemMonitor::SystemMonitor() 
    : running_(false)
    , cpuUsage_(0.0f)
    , memoryUsage_(0.0f)
    , memoryUsageMB_(0.0f) {
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
    
#if defined(USE_CUDA) && defined(HAVE_NVAPI)
    // 如果启用了CUDA并有NVAPI，尝试使用NVAPI初始化
    NvAPI_Status nvStatus = NvAPI_Initialize();
    if (nvStatus == NVAPI_OK) {
        gpuUsageData_.available = true;
        
        // 获取GPU名称和驱动版本
        NvU32 driverVersion = 0;
        NvAPI_ShortString buildBranch;
        nvStatus = NvAPI_SYS_GetDriverAndBranchVersion(&driverVersion, buildBranch);
        if (nvStatus == NVAPI_OK) {
            char driverVersionStr[32];
            snprintf(driverVersionStr, sizeof(driverVersionStr), "%d.%d", 
                    driverVersion / 100, driverVersion % 100);
            gpuUsageData_.driverVersion = driverVersionStr;
        }
        
        // 获取GPU数量
        NvU32 gpuCount = 0;
        NvPhysicalGpuHandle gpuHandles[NVAPI_MAX_PHYSICAL_GPUS];
        nvStatus = NvAPI_EnumPhysicalGPUs(gpuHandles, &gpuCount);
        if (nvStatus == NVAPI_OK && gpuCount > 0) {
            // 获取GPU名称
            NvAPI_ShortString name;
            nvStatus = NvAPI_GPU_GetFullName(gpuHandles[0], name);
            if (nvStatus == NVAPI_OK) {
                gpuUsageData_.gpuName = name;
            }
            
            // 获取GPU内存信息
            NV_DISPLAY_DRIVER_MEMORY_INFO memInfo = {0};
            memInfo.version = NV_DISPLAY_DRIVER_MEMORY_INFO_VER;
            nvStatus = NvAPI_GPU_GetMemoryInfo(gpuHandles[0], &memInfo);
            if (nvStatus == NVAPI_OK) {
                gpuUsageData_.memoryTotalMB = static_cast<float>(memInfo.dedicatedVideoMemory) / 1024.0f;
            }
        }
    }
#endif

    // 尝试使用PDH查询GPU利用率
    status = PdhOpenQuery(NULL, 0, &gpuQuery_);
    if (status == ERROR_SUCCESS) {
        // 尝试添加GPU使用率计数器 (NVIDIA)
        status = PdhAddCounterA(gpuQuery_, "\\GPU Engine(*)\\Utilization Percentage", 0, &gpuCounter_);
        if (status == ERROR_SUCCESS) {
            if (!gpuUsageData_.available) {
                gpuUsageData_.available = true;
            }
            PdhCollectQueryData(gpuQuery_);
        } else {
            // 尝试添加其他GPU计数器
            status = PdhAddCounterA(gpuQuery_, "\\GPU Engine(*engtype_3D)\\Utilization Percentage", 0, &gpuCounter_);
            if (status == ERROR_SUCCESS) {
                if (!gpuUsageData_.available) {
                    gpuUsageData_.available = true;
                }
                PdhCollectQueryData(gpuQuery_);
            } else {
                PdhCloseQuery(gpuQuery_);
            }
        }
    }
#endif

    return true;
}

void SystemMonitor::update() {
    cpuUsage_ = calculateCpuUsage();
    memoryUsage_ = calculateMemoryUsage();
    memoryUsageMB_ = calculateMemoryUsageMB();
    updateCPUUsage();
    updateGPUUsage();
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
    
#if defined(USE_CUDA) && defined(HAVE_NVAPI)
    // 使用NVAPI获取GPU信息
    NvPhysicalGpuHandle gpuHandles[NVAPI_MAX_PHYSICAL_GPUS];
    NvU32 gpuCount = 0;
    NvAPI_Status nvStatus = NvAPI_EnumPhysicalGPUs(gpuHandles, &gpuCount);
    
    if (nvStatus == NVAPI_OK && gpuCount > 0) {
        // 获取GPU内存使用情况
        NV_DISPLAY_DRIVER_MEMORY_INFO memInfo = {0};
        memInfo.version = NV_DISPLAY_DRIVER_MEMORY_INFO_VER;
        nvStatus = NvAPI_GPU_GetMemoryInfo(gpuHandles[0], &memInfo);
        if (nvStatus == NVAPI_OK) {
            float usedMemory = static_cast<float>(memInfo.dedicatedVideoMemory - memInfo.curAvailableDedicatedVideoMemory);
            
            std::lock_guard<std::mutex> lock(gpuUsageData_.mutex);
            gpuUsageData_.memoryUsageMB = usedMemory / 1024.0f;
            gpuUsageData_.memoryTotalMB = static_cast<float>(memInfo.dedicatedVideoMemory) / 1024.0f;
            gpuUsageData_.memoryUsagePercent = (usedMemory / memInfo.dedicatedVideoMemory) * 100.0f;
        }
        
        // 获取GPU温度
        NV_GPU_THERMAL_SETTINGS thermalSettings = {0};
        thermalSettings.version = NV_GPU_THERMAL_SETTINGS_VER;
        nvStatus = NvAPI_GPU_GetThermalSettings(gpuHandles[0], 0, &thermalSettings);
        if (nvStatus == NVAPI_OK && thermalSettings.count > 0) {
            std::lock_guard<std::mutex> lock(gpuUsageData_.mutex);
            gpuUsageData_.temperature = static_cast<float>(thermalSettings.sensor[0].currentTemp);
        }
        
        // 获取GPU利用率
        NV_GPU_DYNAMIC_PSTATES_INFO_EX pstatesInfo = {0};
        pstatesInfo.version = NV_GPU_DYNAMIC_PSTATES_INFO_EX_VER;
        nvStatus = NvAPI_GPU_GetDynamicPstatesInfoEx(gpuHandles[0], &pstatesInfo);
        if (nvStatus == NVAPI_OK) {
            int gpuUtilization = pstatesInfo.utilization[0].percentage;
            
            std::lock_guard<std::mutex> lock(gpuUsageData_.mutex);
            gpuUsageData_.currentUsage = static_cast<float>(gpuUtilization);
            gpuUsageData_.usageHistory.push_back(gpuUsageData_.currentUsage);
            
            // 保持历史记录不超过最大样本数
            while (gpuUsageData_.usageHistory.size() > gpuUsageData_.maxSamples) {
                gpuUsageData_.usageHistory.pop_front();
            }
        }
    }
#endif
    
    // 如果没有NVAPI或通过NVAPI获取失败，使用PDH
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

float SystemMonitor::getMemoryUsageMB() const {
    return memoryUsageMB_;
}

void SystemMonitor::monitorThread() {
    while (running_) {
        cpuUsage_ = calculateCpuUsage();
        memoryUsage_ = calculateMemoryUsage();
        memoryUsageMB_ = calculateMemoryUsageMB();
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

float SystemMonitor::calculateMemoryUsageMB() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        return static_cast<float>(pmc.WorkingSetSize) / (1024.0f * 1024.0f); // 转换为MB
    }
    return 0.0f;
#else
    return 0.0f; // 在非 Windows 系统上暂时返回 0
#endif
}

// 实现新增的GPU相关getter方法
bool SystemMonitor::isGPUAvailable() const {
    return gpuUsageData_.available;
}

float SystemMonitor::getGPUUsage() const {
    return gpuUsageData_.currentUsage;
}

float SystemMonitor::getGPUMemoryUsageMB() const {
    return gpuUsageData_.memoryUsageMB;
}

float SystemMonitor::getGPUMemoryTotalMB() const {
    return gpuUsageData_.memoryTotalMB;
}

float SystemMonitor::getGPUMemoryPercent() const {
    return gpuUsageData_.memoryUsagePercent;
}

float SystemMonitor::getGPUTemperature() const {
    return gpuUsageData_.temperature;
}

std::string SystemMonitor::getGPUName() const {
    return gpuUsageData_.gpuName;
}

std::string SystemMonitor::getGPUDriverVersion() const {
    return gpuUsageData_.driverVersion;
} 