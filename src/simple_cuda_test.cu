#include <stdio.h>
#include <cuda_runtime.h>

// CUDA kernel function for vector addition on GPU
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
    // Print CUDA version information
    int runtimeVersion = 0;
    cudaRuntimeGetVersion(&runtimeVersion);
    printf("CUDA Runtime Version: %d\n", runtimeVersion);
    
    // Print GPU device information
    int deviceCount = 0;
    cudaGetDeviceCount(&deviceCount);
    
    if (deviceCount == 0)
    {
        printf("No CUDA-capable devices found!\n");
        return -1;
    }
    
    printf("Detected %d CUDA devices:\n", deviceCount);
    
    for (int i = 0; i < deviceCount; ++i)
    {
        cudaDeviceProp deviceProp;
        cudaGetDeviceProperties(&deviceProp, i);
        
        printf("Device %d: \"%s\"\n", i, deviceProp.name);
        printf("  Compute Capability: %d.%d\n", deviceProp.major, deviceProp.minor);
        printf("  Total Memory: %.2f GB\n", 
               static_cast<float>(deviceProp.totalGlobalMem) / (1024.0f * 1024.0f * 1024.0f));
        printf("  Multiprocessor Count: %d\n", deviceProp.multiProcessorCount);
    }
    
    // Set array size
    int numElements = 50000;
    size_t size = numElements * sizeof(float);
    printf("\nPerforming vector addition test (%d elements)...\n", numElements);
    
    // Allocate host memory
    float *h_A = (float *)malloc(size);
    float *h_B = (float *)malloc(size);
    float *h_C = (float *)malloc(size);
    
    if (h_A == NULL || h_B == NULL || h_C == NULL)
    {
        fprintf(stderr, "Failed to allocate host memory\n");
        exit(EXIT_FAILURE);
    }
    
    // Initialize data
    for (int i = 0; i < numElements; ++i)
    {
        h_A[i] = rand() / (float)RAND_MAX;
        h_B[i] = rand() / (float)RAND_MAX;
    }
    
    // Allocate device memory
    float *d_A = NULL;
    float *d_B = NULL;
    float *d_C = NULL;
    
    cudaError_t err = cudaMalloc((void **)&d_A, size);
    if (err != cudaSuccess)
    {
        fprintf(stderr, "Failed to allocate device memory (A): %s\n", cudaGetErrorString(err));
        exit(EXIT_FAILURE);
    }
    
    err = cudaMalloc((void **)&d_B, size);
    if (err != cudaSuccess)
    {
        fprintf(stderr, "Failed to allocate device memory (B): %s\n", cudaGetErrorString(err));
        exit(EXIT_FAILURE);
    }
    
    err = cudaMalloc((void **)&d_C, size);
    if (err != cudaSuccess)
    {
        fprintf(stderr, "Failed to allocate device memory (C): %s\n", cudaGetErrorString(err));
        exit(EXIT_FAILURE);
    }
    
    // Copy data from host to device
    printf("Copying data from host to device...\n");
    err = cudaMemcpy(d_A, h_A, size, cudaMemcpyHostToDevice);
    if (err != cudaSuccess)
    {
        fprintf(stderr, "Failed to copy data from host to device (A): %s\n", cudaGetErrorString(err));
        exit(EXIT_FAILURE);
    }
    
    err = cudaMemcpy(d_B, h_B, size, cudaMemcpyHostToDevice);
    if (err != cudaSuccess)
    {
        fprintf(stderr, "Failed to copy data from host to device (B): %s\n", cudaGetErrorString(err));
        exit(EXIT_FAILURE);
    }
    
    // Launch CUDA kernel
    int threadsPerBlock = 256;
    int blocksPerGrid = (numElements + threadsPerBlock - 1) / threadsPerBlock;
    printf("CUDA kernel configuration: %d blocks, %d threads per block\n", blocksPerGrid, threadsPerBlock);
    
    vectorAdd<<<blocksPerGrid, threadsPerBlock>>>(d_A, d_B, d_C, numElements);
    err = cudaGetLastError();
    if (err != cudaSuccess)
    {
        fprintf(stderr, "Failed to launch kernel: %s\n", cudaGetErrorString(err));
        exit(EXIT_FAILURE);
    }
    
    // Wait for GPU to finish
    cudaDeviceSynchronize();
    
    // Copy result back to host
    printf("Copying result back to host...\n");
    err = cudaMemcpy(h_C, d_C, size, cudaMemcpyDeviceToHost);
    if (err != cudaSuccess)
    {
        fprintf(stderr, "Failed to copy data from device to host: %s\n", cudaGetErrorString(err));
        exit(EXIT_FAILURE);
    }
    
    // Verify result
    for (int i = 0; i < numElements; ++i)
    {
        if (fabs(h_A[i] + h_B[i] - h_C[i]) > 1e-5)
        {
            fprintf(stderr, "Result verification failed at %d!\n", i);
            exit(EXIT_FAILURE);
        }
    }
    
    printf("Vector addition test successful!\n");
    
    // Free device memory
    cudaFree(d_A);
    cudaFree(d_B);
    cudaFree(d_C);
    
    // Free host memory
    free(h_A);
    free(h_B);
    free(h_C);
    
    printf("CUDA test completed, all tests passed!\n");
    return 0;
} 