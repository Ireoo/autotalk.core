#include <stdio.h>
#include <cuda_runtime.h>

// CUDA核函数，在GPU上执行向量加法
__global__ void vectorAdd(const float *A, const float *B, float *C, int numElements)
{
    int i = blockDim.x * blockIdx.x + threadIdx.x;
    if (i < numElements)
    {
        C[i] = A[i] + B[i];
    }
}

int main(void)
{
    // 打印CUDA版本信息
    int runtimeVersion = 0;
    cudaRuntimeGetVersion(&runtimeVersion);
    printf("CUDA运行时版本: %d\n", runtimeVersion);
    
    // 打印GPU设备信息
    int deviceCount = 0;
    cudaGetDeviceCount(&deviceCount);
    
    if (deviceCount == 0)
    {
        printf("没有找到支持CUDA的设备!\n");
        return -1;
    }
    
    printf("检测到 %d 个CUDA设备:\n", deviceCount);
    
    for (int i = 0; i < deviceCount; ++i)
    {
        cudaDeviceProp deviceProp;
        cudaGetDeviceProperties(&deviceProp, i);
        
        printf("设备 %d: \"%s\"\n", i, deviceProp.name);
        printf("  计算能力: %d.%d\n", deviceProp.major, deviceProp.minor);
        printf("  总内存: %.2f GB\n", 
               static_cast<float>(deviceProp.totalGlobalMem) / (1024.0f * 1024.0f * 1024.0f));
        printf("  多处理器数量: %d\n", deviceProp.multiProcessorCount);
    }
    
    // 设置数组大小
    int numElements = 50000;
    size_t size = numElements * sizeof(float);
    printf("\n执行向量加法测试 (%d 个元素)...\n", numElements);
    
    // 分配主机内存
    float *h_A = (float *)malloc(size);
    float *h_B = (float *)malloc(size);
    float *h_C = (float *)malloc(size);
    
    if (h_A == NULL || h_B == NULL || h_C == NULL)
    {
        fprintf(stderr, "主机内存分配失败\n");
        exit(EXIT_FAILURE);
    }
    
    // 初始化数据
    for (int i = 0; i < numElements; ++i)
    {
        h_A[i] = rand() / (float)RAND_MAX;
        h_B[i] = rand() / (float)RAND_MAX;
    }
    
    // 分配设备内存
    float *d_A = NULL;
    float *d_B = NULL;
    float *d_C = NULL;
    
    cudaError_t err = cudaMalloc((void **)&d_A, size);
    if (err != cudaSuccess)
    {
        fprintf(stderr, "设备内存分配失败 (A): %s\n", cudaGetErrorString(err));
        exit(EXIT_FAILURE);
    }
    
    err = cudaMalloc((void **)&d_B, size);
    if (err != cudaSuccess)
    {
        fprintf(stderr, "设备内存分配失败 (B): %s\n", cudaGetErrorString(err));
        exit(EXIT_FAILURE);
    }
    
    err = cudaMalloc((void **)&d_C, size);
    if (err != cudaSuccess)
    {
        fprintf(stderr, "设备内存分配失败 (C): %s\n", cudaGetErrorString(err));
        exit(EXIT_FAILURE);
    }
    
    // 将数据从主机复制到设备
    printf("将数据从主机复制到设备...\n");
    err = cudaMemcpy(d_A, h_A, size, cudaMemcpyHostToDevice);
    if (err != cudaSuccess)
    {
        fprintf(stderr, "数据从主机复制到设备失败 (A): %s\n", cudaGetErrorString(err));
        exit(EXIT_FAILURE);
    }
    
    err = cudaMemcpy(d_B, h_B, size, cudaMemcpyHostToDevice);
    if (err != cudaSuccess)
    {
        fprintf(stderr, "数据从主机复制到设备失败 (B): %s\n", cudaGetErrorString(err));
        exit(EXIT_FAILURE);
    }
    
    // 启动CUDA核函数
    int threadsPerBlock = 256;
    int blocksPerGrid = (numElements + threadsPerBlock - 1) / threadsPerBlock;
    printf("CUDA核函数配置: %d 个块，每块 %d 个线程\n", blocksPerGrid, threadsPerBlock);
    
    vectorAdd<<<blocksPerGrid, threadsPerBlock>>>(d_A, d_B, d_C, numElements);
    err = cudaGetLastError();
    if (err != cudaSuccess)
    {
        fprintf(stderr, "核函数启动失败: %s\n", cudaGetErrorString(err));
        exit(EXIT_FAILURE);
    }
    
    // 等待GPU完成
    cudaDeviceSynchronize();
    
    // 将结果从设备复制回主机
    printf("将结果从设备复制回主机...\n");
    err = cudaMemcpy(h_C, d_C, size, cudaMemcpyDeviceToHost);
    if (err != cudaSuccess)
    {
        fprintf(stderr, "数据从设备复制到主机失败: %s\n", cudaGetErrorString(err));
        exit(EXIT_FAILURE);
    }
    
    // 验证结果
    for (int i = 0; i < numElements; ++i)
    {
        if (fabs(h_A[i] + h_B[i] - h_C[i]) > 1e-5)
        {
            fprintf(stderr, "结果验证失败 at %d!\n", i);
            exit(EXIT_FAILURE);
        }
    }
    
    printf("向量加法测试成功!\n");
    
    // 释放设备内存
    cudaFree(d_A);
    cudaFree(d_B);
    cudaFree(d_C);
    
    // 释放主机内存
    free(h_A);
    free(h_B);
    free(h_C);
    
    printf("CUDA测试完成，所有测试通过!\n");
    return 0;
} 