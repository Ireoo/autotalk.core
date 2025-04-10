#!/bin/bash

# 创建构建目录
mkdir -p build_cuda_test
cd build_cuda_test

# 运行CMake生成构建文件
echo "正在生成构建文件..."
cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_COMPILER="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6/bin/nvcc.exe" ../

# 编译项目
echo "正在编译CUDA测试程序..."
cmake --build . --config Release

# 检查编译是否成功
if [ -f "bin/Release/simple_cuda_test.exe" ]; then
    echo "编译成功！正在运行CUDA测试程序..."
    ./bin/Release/simple_cuda_test.exe
else
    echo "编译失败，请检查错误信息。"
fi 