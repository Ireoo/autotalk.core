#include <stdio.h>
#include <cuda_runtime.h>

// CUDA 内核函数，并行将两个数组相加
__global__ void addArrays(int *a, int *b, int *c, int size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < size) {
        c[idx] = a[idx] + b[idx];
    }
}

// 检查CUDA错误并输出错误信息
#define CHECK_CUDA_ERROR(call) \
    do { \
        cudaError_t error = call; \
        if (error != cudaSuccess) { \
            fprintf(stderr, "CUDA错误: %s:%d, %s\n", __FILE__, __LINE__, cudaGetErrorString(error)); \
            exit(EXIT_FAILURE); \
        } \
    } while(0)

// 获取并打印CUDA设备信息
void printDeviceInfo() {
    int deviceCount = 0;
    CHECK_CUDA_ERROR(cudaGetDeviceCount(&deviceCount));
    
    if (deviceCount == 0) {
        printf("未检测到支持CUDA的设备！\n");
        exit(EXIT_FAILURE);
    }
    
    printf("检测到 %d 个CUDA设备:\n", deviceCount);
    
    for (int dev = 0; dev < deviceCount; dev++) {
        cudaDeviceProp deviceProp;
        CHECK_CUDA_ERROR(cudaGetDeviceProperties(&deviceProp, dev));
        
        printf("\n设备 %d: \"%s\"\n", dev, deviceProp.name);
        printf("  CUDA计算能力: %d.%d\n", deviceProp.major, deviceProp.minor);
        printf("  全局内存大小: %.2f GB\n", deviceProp.totalGlobalMem / (1024.0 * 1024.0 * 1024.0));
        printf("  SM数量: %d\n", deviceProp.multiProcessorCount);
        printf("  每个SM的最大线程数: %d\n", deviceProp.maxThreadsPerMultiProcessor);
        printf("  每个线程块的最大线程数: %d\n", deviceProp.maxThreadsPerBlock);
        printf("  最大线程块维度: (%d, %d, %d)\n", 
               deviceProp.maxThreadsDim[0], deviceProp.maxThreadsDim[1], deviceProp.maxThreadsDim[2]);
        printf("  最大网格维度: (%d, %d, %d)\n", 
               deviceProp.maxGridSize[0], deviceProp.maxGridSize[1], deviceProp.maxGridSize[2]);
        printf("  CUDA驱动程序版本: %d.%d\n", deviceProp.major, deviceProp.minor);
    }
}

int main() {
    // 打印CUDA设备信息
    printDeviceInfo();
    
    // 定义数组大小
    const int arraySize = 1000000;
    const int byteSize = arraySize * sizeof(int);
    
    // 分配主机内存
    int *h_a = (int*)malloc(byteSize);
    int *h_b = (int*)malloc(byteSize);
    int *h_c = (int*)malloc(byteSize);
    
    // 初始化数组
    for (int i = 0; i < arraySize; i++) {
        h_a[i] = i;
        h_b[i] = i * 2;
    }
    
    // 分配设备内存
    int *d_a, *d_b, *d_c;
    CHECK_CUDA_ERROR(cudaMalloc(&d_a, byteSize));
    CHECK_CUDA_ERROR(cudaMalloc(&d_b, byteSize));
    CHECK_CUDA_ERROR(cudaMalloc(&d_c, byteSize));
    
    // 将数据从主机复制到设备
    CHECK_CUDA_ERROR(cudaMemcpy(d_a, h_a, byteSize, cudaMemcpyHostToDevice));
    CHECK_CUDA_ERROR(cudaMemcpy(d_b, h_b, byteSize, cudaMemcpyHostToDevice));
    
    // 设置CUDA内核执行配置
    int blockSize = 256;
    int numBlocks = (arraySize + blockSize - 1) / blockSize;
    
    // 计时
    cudaEvent_t start, stop;
    CHECK_CUDA_ERROR(cudaEventCreate(&start));
    CHECK_CUDA_ERROR(cudaEventCreate(&stop));
    
    CHECK_CUDA_ERROR(cudaEventRecord(start));
    
    // 执行CUDA内核
    addArrays<<<numBlocks, blockSize>>>(d_a, d_b, d_c, arraySize);
    
    // 检查内核执行错误
    CHECK_CUDA_ERROR(cudaGetLastError());
    CHECK_CUDA_ERROR(cudaDeviceSynchronize());
    
    CHECK_CUDA_ERROR(cudaEventRecord(stop));
    CHECK_CUDA_ERROR(cudaEventSynchronize(stop));
    
    // 计算经过的时间
    float milliseconds = 0;
    CHECK_CUDA_ERROR(cudaEventElapsedTime(&milliseconds, start, stop));
    
    // 结果复制回主机
    CHECK_CUDA_ERROR(cudaMemcpy(h_c, d_c, byteSize, cudaMemcpyDeviceToHost));
    
    // 验证结果
    bool success = true;
    for (int i = 0; i < arraySize; i++) {
        if (h_c[i] != h_a[i] + h_b[i]) {
            printf("验证失败: h_c[%d] = %d, 期望值 = %d\n", i, h_c[i], h_a[i] + h_b[i]);
            success = false;
            break;
        }
    }
    
    if (success) {
        printf("\n计算成功! 处理 %d 个元素耗时 %.3f ms\n", arraySize, milliseconds);
    }
    
    // 释放设备内存
    CHECK_CUDA_ERROR(cudaFree(d_a));
    CHECK_CUDA_ERROR(cudaFree(d_b));
    CHECK_CUDA_ERROR(cudaFree(d_c));
    
    // 释放主机内存
    free(h_a);
    free(h_b);
    free(h_c);
    
    // 清理CUDA事件
    CHECK_CUDA_ERROR(cudaEventDestroy(start));
    CHECK_CUDA_ERROR(cudaEventDestroy(stop));
    
    return 0;
} 