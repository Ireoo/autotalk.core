#include "nvapi.h"
#include <windows.h>
#include <wbemidl.h>
#include <comdef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// 模拟GPU句柄
static NvPhysicalGpuHandle mockGpuHandles[NVAPI_MAX_PHYSICAL_GPUS];
static NvU32 mockGpuCount = 0;
static bool isInitialized = false;

// 缓存的GPU信息
typedef struct {
    bool valid;
    char name[64];
    NvU32 memory;  // MB
    NvU32 usedMemory; // MB
    int utilization; // 百分比
    int temperature; // 摄氏度
    int power; // 毫瓦
} GpuInfo;

static GpuInfo gpuInfo[NVAPI_MAX_PHYSICAL_GPUS];

// WMI相关变量
static IWbemServices* pSvc = NULL;
static IWbemLocator* pLoc = NULL;

// 辅助函数：从WMI初始化GPU信息
static bool initializeGpuInfoFromWMI() {
    HRESULT hres;
    
    // 初始化COM
    hres = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hres)) {
        return false;
    }
    
    // 初始化COM安全级别
    hres = CoInitializeSecurity(
        NULL, -1, NULL, NULL,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL, EOAC_NONE, NULL
    );
    
    if (FAILED(hres) && hres != RPC_E_TOO_LATE) {
        CoUninitialize();
        return false;
    }
    
    // 创建WMI连接
    hres = CoCreateInstance(
        CLSID_WbemLocator, 0,
        CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, (LPVOID*)&pLoc
    );
    
    if (FAILED(hres)) {
        CoUninitialize();
        return false;
    }
    
    // 连接到WMI
    hres = pLoc->ConnectServer(
        _bstr_t(L"ROOT\\CIMV2"),
        NULL, NULL, NULL, 0, NULL, NULL, &pSvc
    );
    
    if (FAILED(hres)) {
        pLoc->Release();
        CoUninitialize();
        return false;
    }
    
    // 设置代理的安全级别
    hres = CoSetProxyBlanket(
        pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL, EOAC_NONE
    );
    
    if (FAILED(hres)) {
        pSvc->Release();
        pLoc->Release();
        CoUninitialize();
        return false;
    }
    
    // 查询GPU信息
    IEnumWbemClassObject* pEnumerator = NULL;
    hres = pSvc->ExecQuery(
        bstr_t("WQL"),
        bstr_t("SELECT * FROM Win32_VideoController"),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL, &pEnumerator
    );
    
    if (FAILED(hres)) {
        pSvc->Release();
        pLoc->Release();
        CoUninitialize();
        return false;
    }
    
    // 枚举GPU
    IWbemClassObject* pclsObj = NULL;
    ULONG uReturn = 0;
    mockGpuCount = 0;
    
    while (pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn) == S_OK) {
        if (uReturn == 0 || mockGpuCount >= NVAPI_MAX_PHYSICAL_GPUS) break;
        
        // 初始化模拟的GPU句柄
        mockGpuHandles[mockGpuCount] = (NvPhysicalGpuHandle)((size_t)mockGpuCount + 1);
        
        // 初始化GPU信息
        gpuInfo[mockGpuCount].valid = true;
        gpuInfo[mockGpuCount].utilization = 0;
        gpuInfo[mockGpuCount].temperature = 0;
        gpuInfo[mockGpuCount].power = 0;
        
        VARIANT vtProp;
        
        // 获取GPU名称
        if (pclsObj->Get(L"Name", 0, &vtProp, 0, 0) == S_OK) {
            if (vtProp.vt == VT_BSTR) {
                char gpuName[64] = {0};
                WideCharToMultiByte(CP_UTF8, 0, vtProp.bstrVal, -1, gpuName, sizeof(gpuName), NULL, NULL);
                strncpy(gpuInfo[mockGpuCount].name, gpuName, sizeof(gpuInfo[mockGpuCount].name) - 1);
            }
            VariantClear(&vtProp);
        }
        
        // 获取显存大小
        if (pclsObj->Get(L"AdapterRAM", 0, &vtProp, 0, 0) == S_OK) {
            if (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4) {
                gpuInfo[mockGpuCount].memory = vtProp.ulVal / (1024 * 1024); // 转为MB
                gpuInfo[mockGpuCount].usedMemory = gpuInfo[mockGpuCount].memory / 2; // 假设使用了一半
            }
            VariantClear(&vtProp);
        }
        
        mockGpuCount++;
        pclsObj->Release();
    }
    
    pEnumerator->Release();
    return true;
}

// 辅助函数：清理WMI资源
static void cleanupWMI() {
    if (pSvc) {
        pSvc->Release();
        pSvc = NULL;
    }
    
    if (pLoc) {
        pLoc->Release();
        pLoc = NULL;
    }
    
    CoUninitialize();
}

// 辅助函数：更新GPU信息
static void updateGpuInfo() {
    // 在真实实现中，这里会从驱动程序获取最新信息
    // 这里我们只是模拟一些变化
    for (NvU32 i = 0; i < mockGpuCount; i++) {
        if (gpuInfo[i].valid) {
            // 随机模拟利用率变化 (0-100%)
            gpuInfo[i].utilization = rand() % 101;
            
            // 模拟温度 (40-85 摄氏度)
            gpuInfo[i].temperature = 40 + (rand() % 46);
            
            // 模拟功耗 (10-150 瓦)
            gpuInfo[i].power = (10 + (rand() % 141)) * 1000; // 毫瓦
            
            // 模拟内存使用
            gpuInfo[i].usedMemory = (rand() % (gpuInfo[i].memory + 1));
        }
    }
}

// NVAPI 实现
NvAPI_Status NvAPI_Initialize(void) {
    if (isInitialized) {
        return NVAPI_OK;
    }
    
    // 初始化随机数生成器
    srand((unsigned int)GetTickCount());
    
    // 初始化所有GPU信息为无效
    for (int i = 0; i < NVAPI_MAX_PHYSICAL_GPUS; i++) {
        gpuInfo[i].valid = false;
    }
    
    // 从WMI获取GPU信息
    if (initializeGpuInfoFromWMI()) {
        isInitialized = true;
        return NVAPI_OK;
    }
    
    return NVAPI_ERROR;
}

NvAPI_Status NvAPI_EnumPhysicalGPUs(NvPhysicalGpuHandle* gpuHandles, NvU32* gpuCount) {
    if (!isInitialized) {
        return NVAPI_ERROR;
    }
    
    if (!gpuHandles || !gpuCount) {
        return NVAPI_ERROR;
    }
    
    // 更新GPU信息
    updateGpuInfo();
    
    *gpuCount = mockGpuCount;
    for (NvU32 i = 0; i < mockGpuCount; i++) {
        gpuHandles[i] = mockGpuHandles[i];
    }
    
    return NVAPI_OK;
}

NvAPI_Status NvAPI_GPU_GetMemoryInfo(NvPhysicalGpuHandle gpu, NV_DISPLAY_DRIVER_MEMORY_INFO* pMemoryInfo) {
    if (!isInitialized || !pMemoryInfo) {
        return NVAPI_ERROR;
    }
    
    // 检查版本
    if (pMemoryInfo->version != NV_DISPLAY_DRIVER_MEMORY_INFO_VER) {
        return NVAPI_ERROR;
    }
    
    // 从GPU句柄获取索引
    size_t idx = (size_t)gpu - 1;
    if (idx >= mockGpuCount || !gpuInfo[idx].valid) {
        return NVAPI_ERROR;
    }
    
    // 填充内存信息
    pMemoryInfo->dedicatedVideoMemory = gpuInfo[idx].memory * 1024; // 转为KB
    pMemoryInfo->availableDedicatedVideoMemory = gpuInfo[idx].memory * 1024;
    pMemoryInfo->curAvailableDedicatedVideoMemory = (gpuInfo[idx].memory - gpuInfo[idx].usedMemory) * 1024;
    pMemoryInfo->systemVideoMemory = 0;
    pMemoryInfo->sharedSystemMemory = 0;
    
    return NVAPI_OK;
}

NvAPI_Status NvAPI_GPU_GetThermalSettings(NvPhysicalGpuHandle gpu, NvU32 sensorIndex, NV_GPU_THERMAL_SETTINGS* pThermalSettings) {
    if (!isInitialized || !pThermalSettings) {
        return NVAPI_ERROR;
    }
    
    // 检查版本
    if (pThermalSettings->version != NV_GPU_THERMAL_SETTINGS_VER) {
        return NVAPI_ERROR;
    }
    
    // 从GPU句柄获取索引
    size_t idx = (size_t)gpu - 1;
    if (idx >= mockGpuCount || !gpuInfo[idx].valid) {
        return NVAPI_ERROR;
    }
    
    // 填充温度信息
    pThermalSettings->count = 1;
    pThermalSettings->sensor[0].controller = 0;
    pThermalSettings->sensor[0].defaultMinTemp = 0;
    pThermalSettings->sensor[0].defaultMaxTemp = 100;
    pThermalSettings->sensor[0].currentTemp = gpuInfo[idx].temperature;
    pThermalSettings->sensor[0].target = 85;
    
    return NVAPI_OK;
}

NvAPI_Status NvAPI_GPU_GetDynamicPstatesInfoEx(NvPhysicalGpuHandle gpu, NV_GPU_DYNAMIC_PSTATES_INFO_EX* pDynamicPstatesInfoEx) {
    if (!isInitialized || !pDynamicPstatesInfoEx) {
        return NVAPI_ERROR;
    }
    
    // 检查版本
    if (pDynamicPstatesInfoEx->version != NV_GPU_DYNAMIC_PSTATES_INFO_EX_VER) {
        return NVAPI_ERROR;
    }
    
    // 从GPU句柄获取索引
    size_t idx = (size_t)gpu - 1;
    if (idx >= mockGpuCount || !gpuInfo[idx].valid) {
        return NVAPI_ERROR;
    }
    
    // 填充利用率信息
    pDynamicPstatesInfoEx->flags = 0;
    
    // GPU利用率
    pDynamicPstatesInfoEx->utilization[0].bIsPresent = 1;
    pDynamicPstatesInfoEx->utilization[0].percentage = gpuInfo[idx].utilization;
    
    // 其他利用率设为0
    for (int i = 1; i < 8; i++) {
        pDynamicPstatesInfoEx->utilization[i].bIsPresent = 0;
        pDynamicPstatesInfoEx->utilization[i].percentage = 0;
    }
    
    return NVAPI_OK;
}

NvAPI_Status NvAPI_GPU_GetFullName(NvPhysicalGpuHandle gpu, NvAPI_ShortString name) {
    if (!isInitialized || !name) {
        return NVAPI_ERROR;
    }
    
    // 从GPU句柄获取索引
    size_t idx = (size_t)gpu - 1;
    if (idx >= mockGpuCount || !gpuInfo[idx].valid) {
        return NVAPI_ERROR;
    }
    
    // 复制名称
    strncpy(name, gpuInfo[idx].name, 64);
    return NVAPI_OK;
}

NvAPI_Status NvAPI_SYS_GetDriverAndBranchVersion(NvU32* pDriverVersion, NvAPI_ShortString driverBranch) {
    if (!isInitialized || !pDriverVersion || !driverBranch) {
        return NVAPI_ERROR;
    }
    
    // 模拟驱动版本 5.31
    *pDriverVersion = 531;
    strncpy(driverBranch, "模拟版本", 64);
    
    return NVAPI_OK;
}

NvAPI_Status NvAPI_GPU_ClientPowerPoliciesGetStatus(NvPhysicalGpuHandle gpu, NV_GPU_POWER_STATUS* pPowerStatus) {
    if (!isInitialized || !pPowerStatus) {
        return NVAPI_ERROR;
    }
    
    // 检查版本
    if (pPowerStatus->version != NV_GPU_POWER_STATUS_VER) {
        return NVAPI_ERROR;
    }
    
    // 从GPU句柄获取索引
    size_t idx = (size_t)gpu - 1;
    if (idx >= mockGpuCount || !gpuInfo[idx].valid) {
        return NVAPI_ERROR;
    }
    
    // 填充功耗信息
    pPowerStatus->flags = 0;
    pPowerStatus->power = gpuInfo[idx].power; // 毫瓦
    
    return NVAPI_OK;
} 