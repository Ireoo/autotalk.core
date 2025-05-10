#!/bin/bash

# 设置错误时退出
set -e

# 添加命令行参数处理
USE_GPU=0
SKIP_DEPS=0
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
    *)
      shift
      ;;
  esac
done

# 获取CPU核心数用于并行编译
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" ]]; then
    NUM_CORES=$(nproc)
else
    NUM_CORES=$(sysctl -n hw.ncpu || nproc)
fi

echo "==== 开始构建项目 ===="
if [ "$USE_GPU" -eq 1 ]; then
  echo "启用GPU支持构建"
  
  # 检查CUDA是否安装
  if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" ]]; then
    if [ ! -d "/c/Program Files/NVIDIA GPU Computing Toolkit/CUDA" ]; then
      echo "错误: 未找到CUDA安装，请先安装CUDA工具包"
      exit 1
    fi
    
    # 找到最新版本的CUDA
    CUDA_PATH=$(ls -d "/c/Program Files/NVIDIA GPU Computing Toolkit/CUDA/"* | grep -E "v[0-9]+\.[0-9]+" | sort -r | head -n 1)
    if [ -z "$CUDA_PATH" ]; then
      echo "错误: 无法确定CUDA版本"
      exit 1
    fi
    
    echo "找到CUDA安装: $CUDA_PATH"
    export PATH="$CUDA_PATH/bin:$PATH"
    export CUDA_HOME="$CUDA_PATH"
    export CUDA_PATH="$CUDA_PATH"
    
    # 显示nvcc信息
    if [ -f "$CUDA_PATH/bin/nvcc.exe" ]; then
      echo "NVCC版本信息:"
      "$CUDA_PATH/bin/nvcc.exe" --version
    else
      echo "警告: 未找到nvcc.exe"
    fi
  else
    # Linux/MacOS检查
    if ! command -v nvcc &> /dev/null; then
      echo "错误: 未找到nvcc，请确保CUDA工具包已安装"
      exit 1
    fi
    
    # 显示nvcc信息
    echo "NVCC版本信息:"
    nvcc --version
  fi
else
  echo "使用CPU构建"
fi

# 显示当前工作目录
echo "当前工作目录: $(pwd)"

# 检查并下载依赖项
if [ "$SKIP_DEPS" -eq 0 ]; then
    # 已移除PortAudio下载

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

    echo "系统类型: $OSTYPE"

    # 已移除PortAudio构建
fi

mkdir -p build
cd build

# 准备CMake命令并根据GPU选项添加相关参数
CMAKE_ARGS="-DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SHARED_LIBS=ON \
      -DPortAudio_DIR=\"$(pwd)/../portaudio/install/lib/cmake/portaudio\" \
      -DCMAKE_PREFIX_PATH=\"$(pwd)/../portaudio/install\" \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5"

# 添加GPU选项
if [ "$USE_GPU" -eq 1 ]; then
    CMAKE_ARGS="$CMAKE_ARGS -DUSE_GPU=ON -DGGML_CUDA=ON -DGGML_CUBLAS=ON"
    # 显式设置CUDA架构以避免检测问题
    if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" ]]; then
        CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_CUDA_ARCHITECTURES=all-major"
    fi
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
if [ -f "build/Release/autotalk.exe" ]; then
    cp -f build/Release/autotalk.exe Release/
elif [ -f "build/autotalk.exe" ]; then
    cp -f build/autotalk.exe Release/
elif [ -f "build/bin/Release/autotalk.exe" ]; then
    cp -f build/bin/Release/autotalk.exe Release/
else
    echo "错误: 找不到可执行文件"
    exit 1
fi

if [ "$SKIP_DEPS" -eq 0 ]; then
    echo "正在构建 libsndfile..."

    # 进入 libsndfile 目录
    cd third_party/libsndfile || exit 1

    # 清理并创建构建目录
    rm -rf build
    mkdir build
    cd build || exit 1

    # 配置并构建
    cmake .. -G "Visual Studio 17 2022" -A x64 -DBUILD_SHARED_LIBS=ON
    cmake --build . --config Release --parallel $NUM_CORES

    # 创建 Release 目录（如果不存在）
    mkdir -p ../../../Release

    # 复制 DLL 文件
    if [ -f "Release/sndfile.dll" ]; then
        cp Release/sndfile.dll ../../../Release/
        echo "DLL 文件已复制到 Release 目录"
    else
        echo "错误：找不到 sndfile.dll 文件"
        exit 1
    fi

    cd ../../../
fi

# 复制必要的DLL文件
echo "正在复制DLL文件..."

# 复制PortAudio DLL (已移除使用)
# if [ -f "build/portaudio/Release/portaudio.dll" ]; then
#     cp -f build/portaudio/Release/portaudio.dll Release/
# elif [ -f "build/portaudio/Debug/portaudio.dll" ]; then
#     cp -f build/portaudio/Debug/portaudio.dll Release/
# else
#     echo "错误: 找不到portaudio.dll"
#     exit 1
# fi

# 复制whisper和其他必要的DLL文件
if [ -f "build/bin/Release/whisper.dll" ]; then
    cp -f build/bin/Release/whisper.dll Release/
    cp -f build/bin/Release/ggml.dll Release/
    cp -f build/bin/Release/ggml-cpu.dll Release/
    cp -f build/bin/Release/ggml-base.dll Release/
    cp -f build/bin/Release/sndfile.dll Release/
    
    # 如果启用GPU，复制相关DLL
    if [ "$USE_GPU" -eq 1 ]; then
        cp -f build/bin/Release/ggml-cuda.dll Release/ 2>/dev/null || echo "警告: ggml-cuda.dll不存在"
    fi
else
    echo "错误: 找不到whisper.dll"
    exit 1
fi

echo "构建完成！"

if [ "$USE_GPU" -eq 1 ]; then
    echo "==== GPU版本构建完成 ===="
else
    echo "==== CPU版本构建完成 ===="
fi
echo "可执行文件位于 Release 目录中"

# 运行程序
# ./Release/autotalk.exe --list
