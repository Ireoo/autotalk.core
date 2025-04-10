#!/bin/bash

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # 无颜色

# 确保我们在正确的目录
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$DIR"

echo -e "${BLUE}[INFO]${NC} 当前工作目录: $(pwd)"

# 创建构建目录
mkdir -p cuda_simple_test/build
cd cuda_simple_test/build

echo -e "${BLUE}[INFO]${NC} 配置CUDA测试项目..."
cmake .. || { echo -e "${RED}[ERROR]${NC} CMake配置失败"; exit 1; }

echo -e "${BLUE}[INFO]${NC} 编译CUDA测试项目..."
cmake --build . || { echo -e "${RED}[ERROR]${NC} 编译失败"; exit 1; }

echo -e "${GREEN}[SUCCESS]${NC} CUDA测试项目编译完成"
echo -e "${YELLOW}[RUNNING]${NC} 执行CUDA测试程序..."

# 运行测试程序 - 检查构建输出目录
if [ -f "./Debug/cuda_test.exe" ]; then
    ./Debug/cuda_test.exe || { echo -e "${RED}[ERROR]${NC} 程序执行失败"; exit 1; }
elif [ -f "./cuda_test.exe" ]; then
    ./cuda_test.exe || { echo -e "${RED}[ERROR]${NC} 程序执行失败"; exit 1; }
elif [ -f "./Release/cuda_test.exe" ]; then
    ./Release/cuda_test.exe || { echo -e "${RED}[ERROR]${NC} 程序执行失败"; exit 1; }
else
    echo -e "${RED}[ERROR]${NC} 无法找到CUDA测试可执行文件"
    echo -e "${BLUE}[INFO]${NC} 列出当前目录内容:"
    ls -la .
    echo -e "${BLUE}[INFO]${NC} 列出Debug目录内容(如果存在):"
    [ -d "./Debug" ] && ls -la ./Debug
    exit 1
fi

echo -e "${GREEN}[SUCCESS]${NC} CUDA测试完成!" 