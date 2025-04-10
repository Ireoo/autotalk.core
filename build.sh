#!/bin/bash

# 设置默认选项
USE_CUDA=ON
AUTO_DETECT_GPU=ON

# 处理命令行参数
while [[ $# -gt 0 ]]; do
  case $1 in
    --no-cuda)
      USE_CUDA=OFF
      shift
      ;;
    --cuda-version=*)
      CUDA_VERSION="${1#*=}"
      shift
      ;;
    --no-auto-detect)
      AUTO_DETECT_GPU=OFF
      shift
      ;;
    --help|-h)
      echo "构建选项:"
      echo "  --no-cuda           禁用CUDA支持"
      echo "  --cuda-version=X.Y  指定CUDA版本 (例如: 11.0, 10.0)"
      echo "  --no-auto-detect    禁用自动检测GPU架构"
      echo "  --help, -h          显示此帮助信息"
      exit 0
      ;;
    *)
      echo "未知选项: $1"
      echo "使用 --help 查看可用选项"
      exit 1
      ;;
  esac
done

# 创建构建目录
mkdir -p build
cd build

# 检查是否安装了CMake
if ! command -v cmake &> /dev/null; then
    echo "错误: 未找到CMake。请先安装CMake。"
    exit 1
fi

# 构建CMake选项
CMAKE_OPTIONS="-DUSE_CUDA=$USE_CUDA -DAUTO_DETECT_GPU=$AUTO_DETECT_GPU"

# 如果指定了CUDA版本
if [ ! -z "$CUDA_VERSION" ]; then
    echo "使用CUDA版本: $CUDA_VERSION"
    # 在Windows上可能需要设置CUDA路径
    if [ "$(uname)" == "MINGW"* ] || [ "$(uname)" == "MSYS"* ] || [ "$(uname)" == "CYGWIN"* ] || [ -d "/c/" ]; then
        CMAKE_OPTIONS="$CMAKE_OPTIONS -DCUDAToolkit_ROOT=\"C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v$CUDA_VERSION\""
    fi
fi

# 运行CMake配置
echo "配置项目..."
cmake -S .. -B . $CMAKE_OPTIONS || { echo "CMake配置失败"; exit 1; }

# 编译项目
echo "构建项目..."
cmake --build . --config Release || { echo "构建失败"; exit 1; }

echo "构建完成！"
echo "您可以使用以下命令运行程序:"
echo "./autotalk /path/to/model.bin"
echo ""
echo "程序会优先使用GPU运行模型，如果没有可用的GPU，将自动回退到CPU。"
echo ""
echo "如果您还没有下载模型，可以使用download_model.sh脚本下载:"
echo "../download_model.sh small" 