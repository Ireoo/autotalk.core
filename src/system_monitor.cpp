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

// WMI相关头文件
#include <wbemidl.h>
#include <comdef.h>
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

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
    // 获取CPU核心数
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    cpuUsageData_.numCores = sysInfo.dwNumberOfProcessors;
    
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
        // 获取GPU数量
        NvU32 gpuCount = 0;
        NvPhysicalGpuHandle gpuHandles[NVAPI_MAX_PHYSICAL_GPUS];
        nvStatus = NvAPI_EnumPhysicalGPUs(gpuHandles, &gpuCount);
        
        if (nvStatus == NVAPI_OK && gpuCount > 0) {
            // 清空之前的GPU列表
            std::lock_guard<std::mutex> lock(multiGPUInfo_.mutex);
            multiGPUInfo_.gpus.clear();
            
            // 获取GPU驱动版本（所有GPU共用）
            NvU32 driverVersion = 0;
            NvAPI_ShortString buildBranch;
            nvStatus = NvAPI_SYS_GetDriverAndBranchVersion(&driverVersion, buildBranch);
            std::string driverVersionStr = "未知";
            if (nvStatus == NVAPI_OK) {
                char driverVersionBuffer[32];
                snprintf(driverVersionBuffer, sizeof(driverVersionBuffer), "%d.%d", 
                        driverVersion / 100, driverVersion % 100);
                driverVersionStr = driverVersionBuffer;
            }
            
            // 处理每个GPU
            for (NvU32 i = 0; i < gpuCount; i++) {
                GPUUsageData gpuData;
                gpuData.available = true;
                gpuData.gpuIndex = i;
                gpuData.driverVersion = driverVersionStr;
                
                // 获取GPU名称
                NvAPI_ShortString name;
                nvStatus = NvAPI_GPU_GetFullName(gpuHandles[i], name);
                if (nvStatus == NVAPI_OK) {
                    gpuData.gpuName = name;
                }
                
                // 获取GPU内存信息
                NV_DISPLAY_DRIVER_MEMORY_INFO memInfo = {0};
                memInfo.version = NV_DISPLAY_DRIVER_MEMORY_INFO_VER;
                nvStatus = NvAPI_GPU_GetMemoryInfo(gpuHandles[i], &memInfo);
                if (nvStatus == NVAPI_OK) {
                    gpuData.memoryTotalMB = static_cast<float>(memInfo.dedicatedVideoMemory) / 1024.0f;
                }
                
                multiGPUInfo_.gpus.push_back(std::move(gpuData));
            }
            
            // 如果存在GPU，设置第一个作为当前GPU
            if (!multiGPUInfo_.gpus.empty()) {
                gpuUsageData_.copyDataFrom(multiGPUInfo_.gpus[0]);
            }
        }
    }
#else
    // 如果没有NVAPI，尝试使用其他方法获取GPU信息
    #ifdef _WIN32
    // 尝试通过WMI获取GPU信息
    HRESULT hres;
    
    // 初始化COM
    hres = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (SUCCEEDED(hres)) {
        // 初始化COM安全级别
        hres = CoInitializeSecurity(
            NULL, -1, NULL, NULL,
            RPC_C_AUTHN_LEVEL_DEFAULT,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            NULL, EOAC_NONE, NULL
        );
        
        if (SUCCEEDED(hres) || hres == RPC_E_TOO_LATE) {
            // 创建WMI连接
            IWbemLocator *pLoc = NULL;
            hres = CoCreateInstance(
                CLSID_WbemLocator, 0,
                CLSCTX_INPROC_SERVER,
                IID_IWbemLocator, (LPVOID *) &pLoc
            );
            
            if (SUCCEEDED(hres) && pLoc) {
                // 连接到WMI命名空间
                IWbemServices *pSvc = NULL;
                hres = pLoc->ConnectServer(
                    _bstr_t(L"ROOT\\CIMV2"),
                    NULL, NULL, 0, NULL, 0, 0, &pSvc
                );
                
                if (SUCCEEDED(hres) && pSvc) {
                    // 设置安全级别
                    hres = CoSetProxyBlanket(
                        pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
                        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                        NULL, EOAC_NONE
                    );
                    
                    if (SUCCEEDED(hres)) {
                        // 查询GPU信息
                        IEnumWbemClassObject* pEnumerator = NULL;
                        hres = pSvc->ExecQuery(
                            bstr_t("WQL"),
                            bstr_t("SELECT * FROM Win32_VideoController"),
                            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                            NULL,
                            &pEnumerator
                        );
                        
                        if (SUCCEEDED(hres) && pEnumerator) {
                            // 清空之前的GPU列表
                            std::lock_guard<std::mutex> lock(multiGPUInfo_.mutex);
                            multiGPUInfo_.gpus.clear();
                            
                            // 遍历所有GPU
                            IWbemClassObject *pclsObj = NULL;
                            ULONG uReturn = 0;
                            int gpuIndex = 0;
                            
                            while (pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn) == S_OK) {
                                if (uReturn == 0) break;
                                
                                GPUUsageData gpuData;
                                gpuData.available = true;
                                gpuData.gpuIndex = gpuIndex++;
                                VARIANT vtProp;
                                
                                // 获取GPU名称
                                if (pclsObj->Get(L"Name", 0, &vtProp, 0, 0) == S_OK) {
                                    char gpuName[256] = {0};
                                    WideCharToMultiByte(CP_UTF8, 0, vtProp.bstrVal, -1, gpuName, sizeof(gpuName), NULL, NULL);
                                    gpuData.gpuName = gpuName;
                                    VariantClear(&vtProp);
                                }
                                
                                // 获取驱动版本
                                if (pclsObj->Get(L"DriverVersion", 0, &vtProp, 0, 0) == S_OK) {
                                    char driverVersion[256] = {0};
                                    WideCharToMultiByte(CP_UTF8, 0, vtProp.bstrVal, -1, driverVersion, sizeof(driverVersion), NULL, NULL);
                                    gpuData.driverVersion = driverVersion;
                                    VariantClear(&vtProp);
                                }
                                
                                // 获取显存大小 (以字节为单位，转换为MB)
                                if (pclsObj->Get(L"AdapterRAM", 0, &vtProp, 0, 0) == S_OK) {
                                    if (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4) {
                                        gpuData.memoryTotalMB = static_cast<float>(vtProp.ulVal) / (1024.0f * 1024.0f);
                                    }
                                    VariantClear(&vtProp);
                                }
                                
                                multiGPUInfo_.gpus.push_back(std::move(gpuData));
                                pclsObj->Release();
                            }
                            
                            // 如果存在GPU，设置第一个作为当前GPU
                            if (!multiGPUInfo_.gpus.empty()) {
                                gpuUsageData_.copyDataFrom(multiGPUInfo_.gpus[0]);
                            }
                            
                            pEnumerator->Release();
                        }
                    }
                    
                    pSvc->Release();
                }
                
                pLoc->Release();
            }
        }
        
        // 释放COM
        CoUninitialize();
    }
    #endif
#endif

    // 尝试使用PDH查询GPU利用率
    status = PdhOpenQuery(NULL, 0, &gpuQuery_);
    if (status == ERROR_SUCCESS) {
        // 尝试添加GPU使用率计数器 (NVIDIA)
        status = PdhAddCounterA(gpuQuery_, "\\GPU Engine(*)\\Utilization Percentage", 0, &gpuCounter_);
        if (status == ERROR_SUCCESS) {
            if (!gpuUsageData_.available && !multiGPUInfo_.gpus.empty()) {
                gpuUsageData_.available = true;
            }
            PdhCollectQueryData(gpuQuery_);
        } else {
            // 尝试添加其他GPU计数器
            status = PdhAddCounterA(gpuQuery_, "\\GPU Engine(*engtype_3D)\\Utilization Percentage", 0, &gpuCounter_);
            if (status == ERROR_SUCCESS) {
                if (!gpuUsageData_.available && !multiGPUInfo_.gpus.empty()) {
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
    if (!gpuUsageData_.available && multiGPUInfo_.gpus.empty()) {
        return;
    }
    
#if defined(USE_CUDA) && defined(HAVE_NVAPI)
    // 使用NVAPI获取GPU信息
    NvPhysicalGpuHandle gpuHandles[NVAPI_MAX_PHYSICAL_GPUS];
    NvU32 gpuCount = 0;
    NvAPI_Status nvStatus = NvAPI_EnumPhysicalGPUs(gpuHandles, &gpuCount);
    
    if (nvStatus == NVAPI_OK && gpuCount > 0) {
        std::lock_guard<std::mutex> lockMultiGPU(multiGPUInfo_.mutex);
        
        // 确保多GPU信息列表大小与实际GPU数量相符
        if (multiGPUInfo_.gpus.size() != gpuCount) {
            // 如果数量不匹配，重新初始化
            initialize();
            return;
        }
        
        // 更新每个GPU的信息
        for (NvU32 i = 0; i < gpuCount; i++) {
            if (i >= multiGPUInfo_.gpus.size()) continue;
            
            // 获取GPU内存使用情况
            NV_DISPLAY_DRIVER_MEMORY_INFO memInfo = {0};
            memInfo.version = NV_DISPLAY_DRIVER_MEMORY_INFO_VER;
            nvStatus = NvAPI_GPU_GetMemoryInfo(gpuHandles[i], &memInfo);
            if (nvStatus == NVAPI_OK) {
                float usedMemory = static_cast<float>(memInfo.dedicatedVideoMemory - memInfo.curAvailableDedicatedVideoMemory);
                
                multiGPUInfo_.gpus[i].memoryUsageMB = usedMemory / 1024.0f;
                multiGPUInfo_.gpus[i].memoryTotalMB = static_cast<float>(memInfo.dedicatedVideoMemory) / 1024.0f;
                multiGPUInfo_.gpus[i].memoryUsagePercent = (usedMemory / memInfo.dedicatedVideoMemory) * 100.0f;
            }
            
            // 获取GPU温度
            NV_GPU_THERMAL_SETTINGS thermalSettings = {0};
            thermalSettings.version = NV_GPU_THERMAL_SETTINGS_VER;
            nvStatus = NvAPI_GPU_GetThermalSettings(gpuHandles[i], 0, &thermalSettings);
            if (nvStatus == NVAPI_OK && thermalSettings.count > 0) {
                multiGPUInfo_.gpus[i].temperature = static_cast<float>(thermalSettings.sensor[0].currentTemp);
            }
            
            // 获取GPU利用率
            NV_GPU_DYNAMIC_PSTATES_INFO_EX pstatesInfo = {0};
            pstatesInfo.version = NV_GPU_DYNAMIC_PSTATES_INFO_EX_VER;
            nvStatus = NvAPI_GPU_GetDynamicPstatesInfoEx(gpuHandles[i], &pstatesInfo);
            if (nvStatus == NVAPI_OK) {
                int gpuUtilization = pstatesInfo.utilization[0].percentage;
                
                multiGPUInfo_.gpus[i].currentUsage = static_cast<float>(gpuUtilization);
                multiGPUInfo_.gpus[i].usageHistory.push_back(multiGPUInfo_.gpus[i].currentUsage);
                
                // 保持历史记录不超过最大样本数
                while (multiGPUInfo_.gpus[i].usageHistory.size() > multiGPUInfo_.gpus[i].maxSamples) {
                    multiGPUInfo_.gpus[i].usageHistory.pop_front();
                }
            }
        }
        
        // 更新当前选中的GPU信息
        if (!multiGPUInfo_.gpus.empty()) {
            // 找到当前活跃的GPU索引
            int activeGpuIndex = 0;
            if (multiGPUInfo_.activeGPU >= 0 && multiGPUInfo_.activeGPU < static_cast<int>(multiGPUInfo_.gpus.size())) {
                activeGpuIndex = multiGPUInfo_.activeGPU;
            }
            
            // 更新当前GPU信息 - 使用copyDataFrom代替直接赋值
            std::lock_guard<std::mutex> lockGPU(gpuUsageData_.mutex);
            gpuUsageData_.copyDataFrom(multiGPUInfo_.gpus[activeGpuIndex]);
        }
    }
#endif
    
    // 如果没有NVAPI或通过NVAPI获取失败，使用PDH - 这部分仅更新活跃GPU的使用率
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
        std::lock_guard<std::mutex> lockMultiGPU(multiGPUInfo_.mutex);
        if (!multiGPUInfo_.gpus.empty()) {
            int activeGpuIndex = 0;
            if (multiGPUInfo_.activeGPU >= 0 && multiGPUInfo_.activeGPU < static_cast<int>(multiGPUInfo_.gpus.size())) {
                activeGpuIndex = multiGPUInfo_.activeGPU;
            }
            
            multiGPUInfo_.gpus[activeGpuIndex].currentUsage = static_cast<float>(counterVal.doubleValue);
            multiGPUInfo_.gpus[activeGpuIndex].usageHistory.push_back(multiGPUInfo_.gpus[activeGpuIndex].currentUsage);
            
            // 保持历史记录不超过最大样本数
            while (multiGPUInfo_.gpus[activeGpuIndex].usageHistory.size() > multiGPUInfo_.gpus[activeGpuIndex].maxSamples) {
                multiGPUInfo_.gpus[activeGpuIndex].usageHistory.pop_front();
            }
            
            // 更新当前GPU信息
            std::lock_guard<std::mutex> lockGPU(gpuUsageData_.mutex);
            gpuUsageData_.copyDataFrom(multiGPUInfo_.gpus[activeGpuIndex]);
        } else {
            std::lock_guard<std::mutex> lockGPU(gpuUsageData_.mutex);
            gpuUsageData_.currentUsage = static_cast<float>(counterVal.doubleValue);
            gpuUsageData_.usageHistory.push_back(gpuUsageData_.currentUsage);
            
            // 保持历史记录不超过最大样本数
            while (gpuUsageData_.usageHistory.size() > gpuUsageData_.maxSamples) {
                gpuUsageData_.usageHistory.pop_front();
            }
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

// 实现获取CPU核心数的方法
int SystemMonitor::getCPUCores() const {
    return cpuUsageData_.numCores;
}

// 实现获取GPU数量的方法
int SystemMonitor::getGPUCount() const {
    // 使用非const引用
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(multiGPUInfo_.mutex));
    return static_cast<int>(multiGPUInfo_.gpus.size());
}

// 实现获取所有GPU信息的方法
std::vector<GPUUsageData> SystemMonitor::getAllGPUs() const {
    // 创建一个新的向量用于存储GPU数据的副本
    std::vector<GPUUsageData> result;
    {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(multiGPUInfo_.mutex));
        // 为每个GPU创建副本
        for (const auto& gpu : multiGPUInfo_.gpus) {
            result.push_back(gpu.createCopy());
        }
    }
    return result;
}

// 实现获取当前活跃GPU索引的方法
int SystemMonitor::getActiveGPU() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(multiGPUInfo_.mutex));
    return multiGPUInfo_.activeGPU;
}

// 实现设置当前活跃GPU索引的方法
void SystemMonitor::setActiveGPU(int index) {
    std::lock_guard<std::mutex> lock(multiGPUInfo_.mutex);
    if (index >= 0 && index < static_cast<int>(multiGPUInfo_.gpus.size())) {
        multiGPUInfo_.activeGPU = index;
        
        // 更新当前GPU信息
        std::lock_guard<std::mutex> lockGPU(gpuUsageData_.mutex);
        gpuUsageData_.copyDataFrom(multiGPUInfo_.gpus[index]);
    }
} 