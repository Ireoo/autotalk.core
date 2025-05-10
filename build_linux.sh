#!/bin/bash

# 设置错误时退出
set -e

# 添加命令行参数处理
USE_GPU=0
SKIP_DEPS=0
CLEAN_BUILD=0
while [[ $# -gt 0 ]]; do
  case $1 in
    --gpu)
      USE_GPU=1
      shift
      ;;
    --skip-deps)
      SKIP_DEPS=1
      shift
      ;;
    --clean)
      CLEAN_BUILD=1
      shift
      ;;
    *)
      shift
      ;;
  esac
done

# 获取CPU核心数用于并行编译
NUM_CORES=$(nproc)

# 检测系统架构
ARCH=$(uname -m)
echo "检测到系统架构: $ARCH"

echo "==== 开始构建项目 (Linux版本) ===="
if [ "$USE_GPU" -eq 1 ]; then
  echo "启用GPU支持构建"
  
  # 检查CUDA是否安装
  if ! command -v nvcc &> /dev/null; then
    echo "错误: 未找到nvcc，请安装CUDA工具包"
    echo "可执行: sudo apt update && sudo apt install -y nvidia-cuda-toolkit"
    apt update && apt install -y nvidia-cuda-toolkit
  fi
  
  # 显示nvcc信息
  echo "NVCC版本信息:"
  nvcc --version
  
  # 确保安装gcc/g++
  apt update
  apt install -y gcc-12 g++-12
  update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-12 120 \
                           --slave /usr/bin/g++ g++ /usr/bin/g++-12
  
  # 检查并设置CUDA路径
  if [ -d "/usr/local/cuda" ]; then
    CUDA_PATH="/usr/local/cuda"
  else
    CUDA_PATH=$(dirname $(dirname $(which nvcc)))
  fi
  echo "找到CUDA安装: $CUDA_PATH"
  export PATH="$CUDA_PATH/bin:$PATH"
  export CUDA_HOME="$CUDA_PATH"
  export CUDA_PATH="$CUDA_PATH"
  export CUDACXX="$CUDA_PATH/bin/nvcc"
  export CUDA_NVCC_EXECUTABLE="$CUDA_PATH/bin/nvcc"
  # 添加库路径
  export LD_LIBRARY_PATH="$CUDA_PATH/lib64:$LD_LIBRARY_PATH"
else
  echo "使用CPU构建"
fi

# 显示当前工作目录
echo "当前工作目录: $(pwd)"

# 安装必要的依赖
if [ "$SKIP_DEPS" -eq 0 ]; then
    echo "安装系统依赖..."
    # 安装编译工具和库
    apt update
    apt install -y build-essential cmake git libpulse-dev libasound2-dev \
         libflac-dev libogg-dev libvorbis-dev libopus-dev libmp3lame-dev \
         libsndfile1-dev pkg-config
    
    if [ ! -d "whisper.cpp" ]; then
        echo "正在下载whisper.cpp..."
        git clone https://github.com/ggml-org/whisper.cpp.git
    fi

    # 检查并下载 libsndfile
    if [ ! -d "third_party/libsndfile" ]; then
        echo "正在下载 libsndfile..."
        mkdir -p third_party
        git clone https://github.com/libsndfile/libsndfile.git third_party/libsndfile
    fi
fi

# 编译 libsndfile (使用静态链接)
if [ "$SKIP_DEPS" -eq 0 ]; then
    echo "正在构建 libsndfile (静态库)..."
    cd third_party/libsndfile
    rm -rf build
    mkdir -p build
    cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DBUILD_PROGRAMS=OFF
    make -j$NUM_CORES
    cd ../../..
fi

# 创建并进入build目录
if [ "$CLEAN_BUILD" -eq 1 ]; then
    echo "清理build目录..."
    rm -rf build
fi

mkdir -p build
cd build

# 准备CMake命令并根据GPU选项添加相关参数
CMAKE_ARGS="-DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SHARED_LIBS=OFF \
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DLibSndFile_DIR=\"$(pwd)/../third_party/libsndfile/build\" \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5"

# 添加多平台支持定义
echo "添加Linux平台支持..."
CMAKE_ARGS="$CMAKE_ARGS -DPLATFORM_LINUX=ON"

# 添加GPU选项
if [ "$USE_GPU" -eq 1 ]; then
    CMAKE_ARGS="$CMAKE_ARGS -DUSE_GPU=ON -DGGML_CUDA=ON -DGGML_CUBLAS=ON"
    CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_CUDA_HOST_COMPILER=/usr/bin/gcc-12"
    CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_CUDA_COMPILER=$CUDA_PATH/bin/nvcc"
    
    # 根据架构设置不同的CUDA架构
    if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
        echo "为ARM64架构设置CUDA架构参数..."
        # 简化C和CXX标志，移除mcpu设置
        CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_C_FLAGS=-DGGML_USE_ACCELERATE=OFF\ -DGGML_NO_ACCELERATE=ON\ -DGGML_QKK_64=ON\ -DGGML_NO_NATIVE=ON"
        CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_CXX_FLAGS=-DGGML_USE_ACCELERATE=OFF\ -DGGML_NO_ACCELERATE=ON\ -DGGML_QKK_64=ON\ -DGGML_NO_NATIVE=ON"
        
        # 添加ARM特定的禁用选项
        CMAKE_ARGS="$CMAKE_ARGS -DGGML_NO_DOTPROD=ON -DGGML_NO_ARM_SUPPORTS_HINT=ON -DGGML_NO_ACCELERATE=ON -DGGML_ARM_NEON=OFF"
        CMAKE_ARGS="$CMAKE_ARGS -DGGML_NATIVE=OFF -DGGML_CPU_ARM_ARCH=native"
        
        # 常见ARM64 CUDA架构，如Jetson系列使用的是53, 62, 72, 87等
        CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_CUDA_ARCHITECTURES=53;62;72;87"
        CMAKE_ARGS="$CMAKE_ARGS -DCUDA_SEPARABLE_COMPILATION=ON"
        CMAKE_ARGS="$CMAKE_ARGS -DCUDA_RESOLVE_DEVICE_SYMBOLS=ON"
    else
        # x86_64架构的CUDA架构
        echo "为x86_64架构设置CUDA架构参数..."
        CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_CUDA_ARCHITECTURES=75;80;89;90"
    fi
else
    # 仅使用CPU构建
    CMAKE_ARGS="$CMAKE_ARGS -DUSE_GPU=OFF -DGGML_CUDA=OFF -DGGML_CUBLAS=OFF"
fi

# 执行CMake命令
echo "执行CMake命令: cmake $CMAKE_ARGS .."
cmake $CMAKE_ARGS ..

# 构建项目
echo "开始构建项目..."
cmake --build . --config Release --parallel $NUM_CORES

cd ..

# 创建Release目录
rm -rf Release
mkdir -p Release

# 复制可执行文件
if [ -f "build/autotalk" ]; then
    cp -f build/autotalk Release/
elif [ -f "build/bin/autotalk" ]; then
    cp -f build/bin/autotalk Release/
else
    echo "错误: 找不到可执行文件，搜索中..."
    find build -name "autotalk" -type f -exec cp {} Release/ \;
    if [ ! -f "Release/autotalk" ]; then
        echo "未找到可执行文件，编译可能失败"
        exit 1
    fi
fi

echo "构建完成！"

if [ "$USE_GPU" -eq 1 ]; then
    echo "==== GPU版本构建完成 ===="
else
    echo "==== CPU版本构建完成 ===="
fi
echo "可执行文件位于 Release 目录中"

# 设置可执行权限
chmod +x Release/autotalk 