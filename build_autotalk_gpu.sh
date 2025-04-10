#!/bin/bash

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # 无颜色

echo -e "${BLUE}[INFO]${NC} 开始构建GPU版本主程序..."

# 确保我们在正确的目录
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$DIR"

# 创建构建目录
echo -e "${BLUE}[INFO]${NC} 创建构建目录..."
mkdir -p build_autotalk_gpu
cd build_autotalk_gpu

# 生成简化版的CMakeLists.txt
echo -e "${BLUE}[INFO]${NC} 创建简化的CMakeLists.txt..."
cat > CMakeLists.txt << EOL
cmake_minimum_required(VERSION 3.18)
project(AutoTalkGPU LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY \${CMAKE_BINARY_DIR}/bin)

# 查找CUDA包
find_package(CUDA REQUIRED)
include_directories(\${CUDA_INCLUDE_DIRS})

# 定义CUDA支持宏
add_definitions(-DUSE_CUDA)

# 添加whisper.cpp库路径
include_directories(\${CMAKE_SOURCE_DIR}/../whisper.cpp)
include_directories(\${CMAKE_SOURCE_DIR}/../whisper.cpp/include)

# 如果没有预先编译的whisper库，则添加如下代码
file(GLOB WHISPER_SOURCES
    "\${CMAKE_SOURCE_DIR}/../whisper.cpp/whisper.cpp"
    "\${CMAKE_SOURCE_DIR}/../whisper.cpp/examples/common.cpp"
    "\${CMAKE_SOURCE_DIR}/../whisper.cpp/examples/common-ggml.cpp"
)

# 添加音频录制器源文件
file(GLOB AUTOTALK_SOURCES
    "\${CMAKE_SOURCE_DIR}/../src/audio_recorder.cpp"
    "\${CMAKE_SOURCE_DIR}/../src/main.cpp"
)

# 添加可执行文件
add_executable(autotalk_gpu \${AUTOTALK_SOURCES} \${WHISPER_SOURCES})

# 链接库
target_link_libraries(autotalk_gpu PRIVATE \${CUDA_LIBRARIES})

# Windows特定库
if(WIN32)
    target_link_libraries(autotalk_gpu PRIVATE winmm)
endif()

# 输出配置信息
message(STATUS "CUDA version: \${CUDA_VERSION}")
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
cmake --build . --config Release --parallel 4

# 检查编译是否成功
if [ $? -ne 0 ]; then
    echo -e "${RED}[ERROR]${NC} 编译失败!"
    cd ..
    exit 1
fi

# 列出生成的可执行文件
echo -e "${YELLOW}[RESULT]${NC} 生成的可执行文件:"
find bin -type f -name "*.exe" -exec ls -la {} \;

echo -e "${GREEN}[SUCCESS]${NC} GPU版本主程序构建完成!"
echo -e "${YELLOW}[NOTE]${NC} 运行autotalk_gpu需要提供模型文件路径，例如:"
echo -e "${YELLOW}[EXAMPLE]${NC} ./bin/Release/autotalk_gpu.exe ../models/ggml-base.bin" 