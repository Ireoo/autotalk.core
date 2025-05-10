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
NUM_CORES=$(sysctl -n hw.ncpu)

echo "==== 开始构建项目 (macOS版本) ===="
if [ "$USE_GPU" -eq 1 ]; then
  echo "启用Metal GPU支持构建"
  
  # 检查是否安装Xcode命令行工具
  if ! xcode-select -p &> /dev/null; then
    echo "错误: 未找到Xcode命令行工具，请安装"
    echo "可执行: xcode-select --install"
    exit 1
  fi
  
  # 显示Metal开发环境信息
  echo "检查Metal开发环境:"
  if [ -d "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks/Metal.framework" ] || [ -d "$(xcrun --show-sdk-path)/System/Library/Frameworks/Metal.framework" ]; then
    echo "Metal框架已安装"
  else
    echo "警告: 未找到Metal框架，请确保已安装Xcode和命令行工具"
  fi
  
  # 确保安装必要的工具链
  if ! command -v brew &> /dev/null; then
    echo "正在安装Homebrew..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
  fi
  
  # 确保CMake已安装
  if ! command -v cmake &> /dev/null; then
    echo "正在安装CMake..."
    brew install cmake
  fi
  
  # 设置Metal编译环境变量
  export GGML_METAL=1
  export WHISPER_METAL=1
  
else
  echo "使用CPU构建"
fi

# 显示当前工作目录
echo "当前工作目录: $(pwd)"

# 查找Python3解释器路径（提前获取）
PYTHON_PATH=$(which python3)
echo "找到Python解释器: $PYTHON_PATH"

# 安装必要的依赖
if [ "$SKIP_DEPS" -eq 0 ]; then
    echo "安装系统依赖..."
    # 安装编译工具和库
    brew update
    brew install cmake git pkg-config
    brew install libsndfile flac libogg libvorbis opus lame
    
    if [ ! -d "whisper.cpp" ]; then
        echo "正在下载whisper.cpp..."
        git clone https://github.com/ggml-org/whisper.cpp.git
        
        # 修复CMake版本警告
        echo "调整whisper.cpp的CMake最小版本要求..."
        if [ -f "whisper.cpp/CMakeLists.txt" ]; then
            sed -i '' 's/cmake_minimum_required(VERSION 3.0)/cmake_minimum_required(VERSION 3.16)/g' whisper.cpp/CMakeLists.txt
            
            # 修改whisper.cpp的CMakeLists.txt强制使用静态链接
            echo "修改whisper.cpp强制使用静态链接..."
            if grep -q "WHISPER_STATIC" whisper.cpp/CMakeLists.txt; then
                # 已存在选项，修改其值
                sed -i '' 's/option(WHISPER_STATIC[ ]*"[^"]*"[ ]*OFF)/option(WHISPER_STATIC         "静态链接whisper库" ON)/g' whisper.cpp/CMakeLists.txt
            else
                # 不存在选项，使用macOS兼容的方式添加
                # 创建临时文件包含要插入的内容
                TEMP_FILE=$(mktemp)
                echo 'option(WHISPER_STATIC         "静态链接whisper库" ON)' > "$TEMP_FILE"
                
                # 在第一行后插入内容
                sed -i '' -e '1r '"$TEMP_FILE" whisper.cpp/CMakeLists.txt
                
                # 删除临时文件
                rm "$TEMP_FILE"
            fi
        fi
    else
        # 确保已有的whisper.cpp使用静态链接
        if [ -f "whisper.cpp/CMakeLists.txt" ]; then
            echo "更新现有whisper.cpp为静态链接..."
            if grep -q "WHISPER_STATIC" whisper.cpp/CMakeLists.txt; then
                # 已存在选项，修改其值
                sed -i '' 's/option(WHISPER_STATIC[ ]*"[^"]*"[ ]*OFF)/option(WHISPER_STATIC         "静态链接whisper库" ON)/g' whisper.cpp/CMakeLists.txt
            else
                # 不存在选项，使用macOS兼容的方式添加
                # 创建临时文件包含要插入的内容
                TEMP_FILE=$(mktemp)
                echo 'option(WHISPER_STATIC         "静态链接whisper库" ON)' > "$TEMP_FILE"
                
                # 在第一行后插入内容
                sed -i '' -e '1r '"$TEMP_FILE" whisper.cpp/CMakeLists.txt
                
                # 删除临时文件
                rm "$TEMP_FILE"
            fi
        fi
    fi

    # 检查并下载 libsndfile
    if [ ! -d "third_party/libsndfile" ]; then
        echo "正在下载 libsndfile..."
        mkdir -p third_party
        git clone https://github.com/libsndfile/libsndfile.git third_party/libsndfile
    fi
fi

# 编译 libsndfile
if [ "$SKIP_DEPS" -eq 0 ]; then
    echo "正在构建 libsndfile (静态库)..."
    cd third_party/libsndfile
    rm -rf build
    mkdir -p build
    
    # 直接修改源文件而不是使用补丁
    echo "直接修改libsndfile CMake文件..."
    
    # 备份原文件
    cp cmake/SndFileChecks.cmake cmake/SndFileChecks.cmake.bak
    
    # 替换PythonInterp查找代码
    sed -i.bak 's/find_package (PythonInterp REQUIRED)/# 直接设置Python变量\nif (NOT DEFINED PYTHON_EXECUTABLE)\n\tfind_program (PYTHON_EXECUTABLE NAMES python3 python)\nendif ()\nif (PYTHON_EXECUTABLE)\n\tset (PYTHONINTERP_FOUND TRUE)\nendif ()/' cmake/SndFileChecks.cmake
    
    # 固定Python路径引号问题
    sed -i.bak 's/COMMAND "${PYTHON_EXECUTABLE}"/COMMAND ${PYTHON_EXECUTABLE}/' cmake/SndFileChecks.cmake
    
    # 进入build目录
    cd build
    
    # 使用简化的构建选项，强制静态库
    cmake .. -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_EXAMPLES=OFF \
        -DBUILD_TESTING=OFF \
        -DBUILD_PROGRAMS=OFF \
        -DENABLE_TESTING=OFF \
        -DENABLE_CPACK=OFF \
        -DCMAKE_POLICY_DEFAULT_CMP0148=NEW \
        -DPYTHON_EXECUTABLE=${PYTHON_PATH} \
        -Wno-dev
    
    # 编译
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

# 解决可能存在的CMake版本警告
echo "检查并修复CMake版本警告..."
if [ -d "_deps/json-src" ] && [ -f "_deps/json-src/CMakeLists.txt" ]; then
    echo "修复json库CMake版本..."
    sed -i '' 's/cmake_minimum_required(VERSION 3.1)/cmake_minimum_required(VERSION 3.16)/g' _deps/json-src/CMakeLists.txt
fi

# 准备CMake命令并根据GPU选项添加相关参数
CMAKE_ARGS="-DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SHARED_LIBS=OFF \
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DLibSndFile_DIR=\"$(pwd)/../third_party/libsndfile/build\" \
      -DPYTHON_EXECUTABLE=${PYTHON_PATH} \
      -DCMAKE_POLICY_DEFAULT_CMP0148=NEW \
      -DCMAKE_POLICY_DEFAULT_CMP0135=NEW \
      -DCMAKE_POLICY_DEFAULT_CMP0077=NEW \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.16 \
      -Wno-dev"

# 添加多平台支持定义
echo "添加macOS平台支持..."
CMAKE_ARGS="$CMAKE_ARGS -DPLATFORM_MACOS=ON"

# 添加GPU选项
if [ "$USE_GPU" -eq 1 ]; then
    CMAKE_ARGS="$CMAKE_ARGS -DUSE_GPU=ON -DGGML_METAL=ON"
    # 启用Metal支持的其他选项
    CMAKE_ARGS="$CMAKE_ARGS -DWHISPER_METAL=ON"
    # 在macOS上明确禁用CUDA
    CMAKE_ARGS="$CMAKE_ARGS -DUSE_CUDA=OFF -DCUDA_ENABLED=OFF"
else
    # 仅使用CPU构建
    CMAKE_ARGS="$CMAKE_ARGS -DUSE_GPU=OFF -DGGML_METAL=OFF"
fi

# 强制静态链接
echo "启用静态链接..."
CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_FIND_LIBRARY_SUFFIXES=.a"
CMAKE_ARGS="$CMAKE_ARGS -DWHISPER_STATIC=ON"

# 设置rpath以应对仍然需要动态链接的Metal库
CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_INSTALL_RPATH=@executable_path;@executable_path/lib"
CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON"

# 执行CMake命令
echo "执行CMake命令: cmake $CMAKE_ARGS .."
cmake $CMAKE_ARGS ..

# 强制修改生成的编译配置，确保静态链接
if [ -f "CMakeCache.txt" ]; then
    echo "强制修改CMakeCache.txt确保静态链接..."
    sed -i '' 's/BUILD_SHARED_LIBS:BOOL=ON/BUILD_SHARED_LIBS:BOOL=OFF/g' CMakeCache.txt
    sed -i '' 's/WHISPER_STATIC:BOOL=OFF/WHISPER_STATIC:BOOL=ON/g' CMakeCache.txt
fi

# 构建项目
echo "开始构建项目..."
cmake --build . --config Release --parallel $NUM_CORES

cd ..

# 创建Release目录
rm -rf Release
mkdir -p Release
mkdir -p Release/lib

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

# 检查是否需要动态库
echo "检查可执行文件的依赖..."
if otool -L Release/autotalk | grep -q "@rpath"; then
    echo "发现动态库依赖，复制必要的库文件..."
    
    # 复制依赖的动态库文件
    find build -name "*.dylib" -type f -exec cp {} Release/lib/ \;
    
    # 修复动态库的引用路径
    echo "修复可执行文件的动态库引用路径..."
    if [ -f "Release/autotalk" ]; then
        # 列出所有依赖的动态库
        otool -L Release/autotalk | grep dylib | grep @rpath
        
        # 修复所有依赖库的路径
        echo "修复 libwhisper.1.dylib 的引用路径..."
        install_name_tool -change "@rpath/libwhisper.1.dylib" "@executable_path/lib/libwhisper.1.7.5.dylib" Release/autotalk
        
        echo "修复 libsndfile.1.dylib 的引用路径..."
        install_name_tool -change "@rpath/libsndfile.1.dylib" "@executable_path/lib/libsndfile.1.0.37.dylib" Release/autotalk
        
        echo "修复 libggml*.dylib 的引用路径..."
        install_name_tool -change "@rpath/libggml.dylib" "@executable_path/lib/libggml.dylib" Release/autotalk
        install_name_tool -change "@rpath/libggml-cpu.dylib" "@executable_path/lib/libggml-cpu.dylib" Release/autotalk
        install_name_tool -change "@rpath/libggml-blas.dylib" "@executable_path/lib/libggml-blas.dylib" Release/autotalk
        install_name_tool -change "@rpath/libggml-metal.dylib" "@executable_path/lib/libggml-metal.dylib" Release/autotalk
        install_name_tool -change "@rpath/libggml-base.dylib" "@executable_path/lib/libggml-base.dylib" Release/autotalk
        
        # 修复库之间的相互引用
        echo "修复库之间的相互引用..."
        for lib in Release/lib/*.dylib; do
            if [ -f "$lib" ]; then
                # 列出该库依赖的其他库
                deps=$(otool -L "$lib" | grep @rpath | awk '{print $1}')
                
                # 修复每个依赖
                for dep in $deps; do
                    basename=$(basename "$dep")
                    if [ -f "Release/lib/$basename" ]; then
                        echo "修复 $lib 中对 $basename 的引用"
                        install_name_tool -change "$dep" "@loader_path/$basename" "$lib"
                    else
                        echo "警告: 找不到依赖库 $basename"
                    fi
                done
            fi
        done
    fi
else
    echo "成功使用静态链接，无需复制动态库"
    rm -rf Release/lib  # 不需要lib目录
fi

# 如果有Metal着色器文件，也复制到Release目录
if [ "$USE_GPU" -eq 1 ]; then
    echo "复制Metal着色器文件..."
    find build -name "*.metallib" -type f -exec cp {} Release/ \;
fi

echo "构建完成！"

if [ "$USE_GPU" -eq 1 ]; then
    echo "==== GPU版本构建完成 (Metal) ===="
else
    echo "==== CPU版本构建完成 ===="
fi
echo "可执行文件位于 Release 目录中"