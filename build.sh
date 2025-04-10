#!/bin/bash

# 创建构建目录
mkdir -p build
cd build

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

# 配置CMake选项
echo -e "${YELLOW}正在配置构建...${NC}"
cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_COMPILER="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6/bin/nvcc.exe" ..

# 编译项目
echo -e "${YELLOW}正在编译...${NC}"
cmake --build . --config Release

# 检查编译是否成功
if [ -f "bin/Release/simple_cuda_test.exe" ]; then
    echo -e "${GREEN}编译成功！${NC}"
    echo -e "${YELLOW}正在运行CUDA测试程序...${NC}"
    ./bin/Release/simple_cuda_test.exe
else
    echo -e "${RED}编译失败，请检查错误信息。${NC}"
    exit 1
fi 