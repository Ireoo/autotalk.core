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

# 检查CUDA是否可用
echo -e "${BLUE}[INFO]${NC} 检查CUDA环境..."
if command -v nvcc &> /dev/null; then
    nvcc --version
    echo -e "${GREEN}[SUCCESS]${NC} CUDA可用 ✓"
else
    echo -e "${RED}[ERROR]${NC} 未找到CUDA编译器，请确保CUDA已正确安装!"
    exit 1
fi

# 首先确保whisper.cpp子模块已初始化
if [ ! -f "whisper.cpp/CMakeLists.txt" ]; then
    echo -e "${YELLOW}[WARNING]${NC} whisper.cpp子模块不存在或未初始化"
    echo -e "${BLUE}[INFO]${NC} 尝试初始化whisper.cpp子模块..."
    git submodule update --init --recursive
    
    if [ ! -f "whisper.cpp/CMakeLists.txt" ]; then
        echo -e "${RED}[ERROR]${NC} 初始化whisper.cpp子模块失败"
        exit 1
    fi
fi

# 创建构建目录
echo -e "${BLUE}[INFO]${NC} 创建构建目录..."
mkdir -p build_gpu
cd build_gpu

# 配置项目
echo -e "${BLUE}[INFO]${NC} 配置CMake项目..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DGGML_CUDA=ON \
    -DCMAKE_CUDA_ARCHITECTURES=75 \
    -DUSE_CUDA=ON

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

# 回到主目录
cd ..

echo -e "${GREEN}[SUCCESS]${NC} GPU版本主程序构建完成!"
echo -e "${BLUE}[INFO]${NC} 可执行文件位置: build_gpu/bin/Release"

# 列出生成的可执行文件
echo -e "${YELLOW}[RESULT]${NC} 生成的可执行文件:"
find build_gpu/bin -type f -name "*.exe" -exec ls -la {} \;

echo -e "${BLUE}[INFO]${NC} 构建过程完成 ✓"
echo -e "${YELLOW}[NOTE]${NC} 运行autotalk需要提供模型文件路径，例如:"
echo -e "${YELLOW}[EXAMPLE]${NC} ./build_gpu/bin/Release/autotalk.exe ./models/ggml-base.bin" 