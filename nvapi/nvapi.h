#pragma once

// 简化版NVAPI接口，用于在没有NVIDIA NVAPI SDK的情况下编译
// 这不是完整的NVAPI实现，只是提供必要的结构和定义以使代码能够编译

#ifdef __cplusplus
extern "C" {
#endif

// 基本类型定义
typedef unsigned int NvU32;
typedef int NvS32;
typedef unsigned char NvU8;
typedef void* NvPhysicalGpuHandle;
typedef char NvAPI_ShortString[64];

// 状态码定义
typedef enum {
    NVAPI_OK = 0,
    NVAPI_ERROR = -1,
    NVAPI_NVIDIA_DEVICE_NOT_FOUND = -2,
    NVAPI_NOT_SUPPORTED = -3
} NvAPI_Status;

// 常量定义
#define NVAPI_MAX_PHYSICAL_GPUS 64

// 结构体版本定义
#define NV_DISPLAY_DRIVER_MEMORY_INFO_VER 0x03
#define NV_GPU_THERMAL_SETTINGS_VER 0x02
#define NV_GPU_DYNAMIC_PSTATES_INFO_EX_VER 0x01
#define NV_GPU_POWER_STATUS_VER 0x01

// 显存信息结构体
typedef struct {
    NvU32 version;
    NvU32 dedicatedVideoMemory;
    NvU32 availableDedicatedVideoMemory;
    NvU32 curAvailableDedicatedVideoMemory;
    NvU32 systemVideoMemory;
    NvU32 sharedSystemMemory;
} NV_DISPLAY_DRIVER_MEMORY_INFO;

// 温度传感器结构体
typedef struct {
    NvU32 controller;
    NvU32 defaultMinTemp;
    NvU32 defaultMaxTemp;
    NvU32 currentTemp;
    NvU32 target;
} NV_GPU_THERMAL_SENSOR;

// 温度设置结构体
typedef struct {
    NvU32 version;
    NvU32 count;
    NV_GPU_THERMAL_SENSOR sensor[3];
} NV_GPU_THERMAL_SETTINGS;

// GPU利用率状态结构体
typedef struct {
    NvU32 bIsPresent;
    NvU32 percentage;
} NV_GPU_PSTATE_UTILIZATION;

// 动态P-State信息结构体
typedef struct {
    NvU32 version;
    NvU32 flags;
    NV_GPU_PSTATE_UTILIZATION utilization[8];
} NV_GPU_DYNAMIC_PSTATES_INFO_EX;

// 功耗状态结构体
typedef struct {
    NvU32 version;
    NvU32 flags;
    NvU32 power;
} NV_GPU_POWER_STATUS;

// 函数声明
NvAPI_Status NvAPI_Initialize(void);
NvAPI_Status NvAPI_EnumPhysicalGPUs(NvPhysicalGpuHandle*, NvU32*);
NvAPI_Status NvAPI_GPU_GetMemoryInfo(NvPhysicalGpuHandle, NV_DISPLAY_DRIVER_MEMORY_INFO*);
NvAPI_Status NvAPI_GPU_GetThermalSettings(NvPhysicalGpuHandle, NvU32, NV_GPU_THERMAL_SETTINGS*);
NvAPI_Status NvAPI_GPU_GetDynamicPstatesInfoEx(NvPhysicalGpuHandle, NV_GPU_DYNAMIC_PSTATES_INFO_EX*);
NvAPI_Status NvAPI_GPU_GetFullName(NvPhysicalGpuHandle, NvAPI_ShortString);
NvAPI_Status NvAPI_SYS_GetDriverAndBranchVersion(NvU32*, NvAPI_ShortString);
NvAPI_Status NvAPI_GPU_ClientPowerPoliciesGetStatus(NvPhysicalGpuHandle, NV_GPU_POWER_STATUS*);

#ifdef __cplusplus
}
#endif 