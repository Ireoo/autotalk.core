#!/bin/bash

# 清理之前的构建文件
echo "正在清理之前的构建文件..."
rm -rf build_cuda_test

# 创建新的构建目录
mkdir -p build_cuda_test
cd build_cuda_test

# 定义颜色
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # 恢复颜色

# 显示CUDA信息
echo -e "${GREEN}正在检查CUDA环境...${NC}"
if command -v nvcc &> /dev/null; then
    nvcc --version
    echo -e "${GREEN}CUDA可用√${NC}"
else
    echo -e "${RED}未找到CUDA编译器！请确保CUDA已安装并且nvcc在PATH中。${NC}"
    exit 1
fi

# 配置简化版的CMakeLists.txt
cat > CMakeLists.txt << EOL
cmake_minimum_required(VERSION 3.18)
project(SimpleCudaTest LANGUAGES CXX CUDA)

# 设置CUDA架构（Turing架构，适用于RTX 2070）
set(CMAKE_CUDA_ARCHITECTURES 75)

# 设置CUDA标准
set(CMAKE_CUDA_STANDARD 14)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY \${CMAKE_BINARY_DIR}/bin)

# 添加可执行文件
add_executable(simple_cuda_test ../src/simple_cuda_test.cu)

# 输出配置信息
message(STATUS "CUDA版本: \${CMAKE_CUDA_COMPILER_VERSION}")
message(STATUS "目标CUDA架构: \${CMAKE_CUDA_ARCHITECTURES}")
EOL

# 配置CMake选项
echo -e "${YELLOW}正在配置构建...${NC}"
cmake -DCMAKE_BUILD_TYPE=Release .

# 编译项目
echo -e "${YELLOW}正在编译...${NC}"
cmake --build . --config Release

# 检查编译是否成功
CUDA_TEST_EXE=""
if [ -f "bin/Release/simple_cuda_test.exe" ]; then
    CUDA_TEST_EXE="bin/Release/simple_cuda_test.exe"
elif [ -f "bin/simple_cuda_test.exe" ]; then
    CUDA_TEST_EXE="bin/simple_cuda_test.exe"
elif [ -f "Release/simple_cuda_test.exe" ]; then
    CUDA_TEST_EXE="Release/simple_cuda_test.exe"
elif [ -f "simple_cuda_test.exe" ]; then
    CUDA_TEST_EXE="./simple_cuda_test.exe"
fi

if [ ! -z "$CUDA_TEST_EXE" ]; then
    echo -e "${GREEN}编译成功！${NC}"
    echo -e "${YELLOW}正在运行CUDA测试程序...${NC}"
    $CUDA_TEST_EXE
else
    echo -e "${RED}编译失败，请检查错误信息。${NC}"
    # 列出所有可能的路径
    echo -e "${YELLOW}尝试查找可执行文件...${NC}"
    find . -name "*.exe" -type f
    exit 1
fi 