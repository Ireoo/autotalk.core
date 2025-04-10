#include <iostream>
#include <cuda_runtime.h>

// CUDA 核函数，在GPU上执行
__global__ void vectorAdd(float *a, float *b, float *c, int n)
{
    int i = blockDim.x * blockIdx.x + threadIdx.x;
    if (i < n)
    {
        c[i] = a[i] + b[i];
    }
}

int main()
{
    // 输出GPU信息
    int deviceCount = 0;
    cudaError_t error = cudaGetDeviceCount(&deviceCount);
    
    if (error != cudaSuccess)
    {
        std::cerr << "无法获取CUDA设备数量: " << cudaGetErrorString(error) << std::endl;
        return 1;
    }
    
    if (deviceCount == 0)
    {
        std::cerr << "未检测到CUDA设备，请确保您的系统有支持CUDA的GPU并且驱动正确安装" << std::endl;
        return 1;
    }
    
    std::cout << "检测到 " << deviceCount << " 个CUDA设备" << std::endl;
    
    // 打印每个CUDA设备的详细信息
    for (int i = 0; i < deviceCount; ++i)
    {
        cudaDeviceProp deviceProp;
        cudaGetDeviceProperties(&deviceProp, i);
        
        std::cout << "设备 " << i << ": " << deviceProp.name << std::endl;
        std::cout << "  CUDA计算能力: " << deviceProp.major << "." << deviceProp.minor << std::endl;
        std::cout << "  全局内存: " << deviceProp.totalGlobalMem / (1024 * 1024) << " MB" << std::endl;
        std::cout << "  每SM的最大线程数: " << deviceProp.maxThreadsPerMultiProcessor << std::endl;
        std::cout << "  每块的最大线程数: " << deviceProp.maxThreadsPerBlock << std::endl;
        std::cout << "  多处理器数量: " << deviceProp.multiProcessorCount << std::endl;
        std::cout << "  时钟频率: " << deviceProp.clockRate / 1000 << " MHz" << std::endl;
    }
    
    // 执行简单的CUDA向量加法测试
    const int arraySize = 5;
    const int byteSize = arraySize * sizeof(float);
    
    // 主机内存分配
    float h_a[arraySize] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    float h_b[arraySize] = {10.0f, 20.0f, 30.0f, 40.0f, 50.0f};
    float h_c[arraySize] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    
    // 设备内存分配
    float *d_a = NULL;
    float *d_b = NULL;
    float *d_c = NULL;
    
    cudaMalloc((void **)&d_a, byteSize);
    cudaMalloc((void **)&d_b, byteSize);
    cudaMalloc((void **)&d_c, byteSize);
    
    // 将数据从主机复制到设备
    cudaMemcpy(d_a, h_a, byteSize, cudaMemcpyHostToDevice);
    cudaMemcpy(d_b, h_b, byteSize, cudaMemcpyHostToDevice);
    
    // 启动内核
    int threadsPerBlock = 256;
    int blocksPerGrid = (arraySize + threadsPerBlock - 1) / threadsPerBlock;
    std::cout << "CUDA内核启动配置: 每块" << threadsPerBlock << "个线程，共" << blocksPerGrid << "个块" << std::endl;
    
    vectorAdd<<<blocksPerGrid, threadsPerBlock>>>(d_a, d_b, d_c, arraySize);
    
    // 检查内核启动是否有错误
    error = cudaGetLastError();
    if (error != cudaSuccess)
    {
        std::cerr << "CUDA内核启动失败: " << cudaGetErrorString(error) << std::endl;
        
        cudaFree(d_a);
        cudaFree(d_b);
        cudaFree(d_c);
        
        return 1;
    }
    
    // 将结果从设备复制到主机
    cudaMemcpy(h_c, d_c, byteSize, cudaMemcpyDeviceToHost);
    
    // 验证结果
    std::cout << "向量加法结果:" << std::endl;
    for (int i = 0; i < arraySize; ++i)
    {
        std::cout << h_a[i] << " + " << h_b[i] << " = " << h_c[i] << std::endl;
        if (fabs(h_c[i] - (h_a[i] + h_b[i])) > 1e-5)
        {
            std::cerr << "结果验证失败在索引 " << i << std::endl;
            
            cudaFree(d_a);
            cudaFree(d_b);
            cudaFree(d_c);
            
            return 1;
        }
    }
    
    // 释放设备内存
    cudaFree(d_a);
    cudaFree(d_b);
    cudaFree(d_c);
    
    std::cout << "CUDA测试成功完成!" << std::endl;
    return 0;
}
