#!/bin/bash

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # 无颜色

echo -e "${BLUE}[INFO]${NC} 开始构建简化CUDA测试程序..."

# 确保我们在正确的目录
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$DIR"

# 创建构建目录
echo -e "${BLUE}[INFO]${NC} 创建构建目录..."
mkdir -p build_cuda_simple
cd build_cuda_simple

# 生成简化版的CMakeLists.txt
echo -e "${BLUE}[INFO]${NC} 创建简化的CMakeLists.txt..."
cat > CMakeLists.txt << EOL
cmake_minimum_required(VERSION 3.18)
project(SimpleCudaTest LANGUAGES CXX CUDA)

# 设置CUDA架构（适用于RTX系列GPU）
set(CMAKE_CUDA_ARCHITECTURES 75)

# 设置CUDA标准
set(CMAKE_CUDA_STANDARD 14)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# 设置C++标准
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找CUDA包
find_package(CUDA REQUIRED)
include_directories(\${CUDA_INCLUDE_DIRS})

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY \${CMAKE_BINARY_DIR}/bin)

# 定义CUDA支持宏
add_definitions(-DUSE_CUDA)

# 添加可执行文件
add_executable(simple_cuda_test ../src/simple_cuda_test.cu)
add_executable(cuda_test ../src/cuda_test.cpp)

# 设置编译属性
set_target_properties(simple_cuda_test PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    POSITION_INDEPENDENT_CODE ON
)

# 链接CUDA库
target_link_libraries(cuda_test PRIVATE \${CUDA_LIBRARIES})
target_link_libraries(simple_cuda_test PRIVATE \${CUDA_LIBRARIES} \${CUDA_CUDART_LIBRARY})

# 输出配置信息
message(STATUS "CUDA version: \${CMAKE_CUDA_COMPILER_VERSION}")
message(STATUS "Target CUDA architecture: \${CMAKE_CUDA_ARCHITECTURES}")
message(STATUS "CUDA libraries: \${CUDA_LIBRARIES}")
EOL

# 配置项目
echo -e "${BLUE}[INFO]${NC} 配置CMake项目..."
cmake . -DCMAKE_BUILD_TYPE=Release

# 检查CMake配置是否成功
if [ $? -ne 0 ]; then
    echo -e "${RED}[ERROR]${NC} CMake配置失败!"
    cd ..
    exit 1
fi

# 编译项目
echo -e "${BLUE}[INFO]${NC} 编译项目..."
cmake --build . --config Release

# 检查编译是否成功
if [ $? -ne 0 ]; then
    echo -e "${RED}[ERROR]${NC} 编译失败!"
    cd ..
    exit 1
fi

# 列出生成的可执行文件
echo -e "${YELLOW}[RESULT]${NC} 生成的可执行文件:"
find bin -type f -name "*.exe" -exec ls -la {} \;

# 运行CUDA测试程序
echo -e "${BLUE}[INFO]${NC} 运行CUDA测试程序..."
if [ -f "bin/Release/cuda_test.exe" ]; then
    ./bin/Release/cuda_test.exe
elif [ -f "bin/cuda_test.exe" ]; then
    ./bin/cuda_test.exe
else
    echo -e "${RED}[ERROR]${NC} 无法找到CUDA测试可执行文件"
    ls -la bin
    ls -la bin/Release 2>/dev/null
fi

echo -e "${GREEN}[SUCCESS]${NC} CUDA测试完成!" 