#include <iostream>
#include <cuda_runtime.h>

int main() {
    std::cout << "=== CUDA支持检测测试 ===" << std::endl;

    // 检查编译时CUDA支持
#ifdef USE_CUDA
    std::cout << "编译时CUDA支持已启用" << std::endl;
#else
    std::cout << "编译时CUDA支持未启用" << std::endl;
#endif

    // 打印CUDA版本信息
    int runtimeVersion = 0;
    cudaRuntimeGetVersion(&runtimeVersion);
    std::cout << "CUDA运行时版本: " << runtimeVersion << std::endl;
    
    // 打印GPU设备信息
    int deviceCount = 0;
    cudaGetDeviceCount(&deviceCount);
    
    if (deviceCount == 0) {
        std::cout << "没有找到支持CUDA的设备!" << std::endl;
        return -1;
    }
    
    std::cout << "检测到 " << deviceCount << " 个CUDA设备:" << std::endl;
    
    for (int i = 0; i < deviceCount; ++i) {
        cudaDeviceProp deviceProp;
        cudaGetDeviceProperties(&deviceProp, i);
        
        std::cout << "设备 " << i << ": \"" << deviceProp.name << "\"" << std::endl;
        std::cout << "  计算能力: " << deviceProp.major << "." << deviceProp.minor << std::endl;
        std::cout << "  总内存: " << static_cast<float>(deviceProp.totalGlobalMem) / (1024.0f * 1024.0f * 1024.0f) << " GB" << std::endl;
        std::cout << "  多处理器数量: " << deviceProp.multiProcessorCount << std::endl;
    }

    std::cout << "CUDA测试完成，所有测试通过!" << std::endl;
    return 0;
}
