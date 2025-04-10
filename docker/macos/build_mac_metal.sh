#!/bin/bash

# 设置错误处理
set -e

# 创建构建目录
mkdir -p build_metal
cd build_metal

# 配置Metal版本
export LLVM_PATH=$(brew --prefix llvm)
export CC=$LLVM_PATH/bin/clang
export CXX=$LLVM_PATH/bin/clang++
export LDFLAGS="-L$LLVM_PATH/lib -Wl,-rpath,$LLVM_PATH/lib"
export CPPFLAGS="-I$LLVM_PATH/include"

echo "创建Metal版的CMakeLists.txt"
cat > ../CMakeLists.txt.metal << EOL
cmake_minimum_required(VERSION 3.18)
project(AutoTalkMetal LANGUAGES CXX)

# 设置模块路径
list(APPEND CMAKE_MODULE_PATH \${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# 设置C++标准
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY \${CMAKE_BINARY_DIR}/bin)

# 添加whisper.cpp库路径
add_subdirectory(whisper.cpp)
include_directories(\${CMAKE_CURRENT_SOURCE_DIR}/whisper.cpp)
include_directories(\${CMAKE_CURRENT_SOURCE_DIR}/whisper.cpp/include)

# 禁用CUDA, 启用Metal
option(GGML_CUDA "Enable CUDA for GPU acceleration" OFF)
option(GGML_METAL "Enable Metal for GPU acceleration on macOS" ON)

# 添加源文件
add_executable(autotalk src/main.cpp src/audio_recorder.cpp)

# 链接autotalk所需的库
target_link_libraries(autotalk PRIVATE whisper)

if(GGML_METAL)
    target_compile_definitions(autotalk PRIVATE GGML_USE_METAL)
    message(STATUS "启用Metal加速")
endif()
EOL

# 备份和替换CMakeLists.txt
cp ../CMakeLists.txt ../CMakeLists.txt.original
cp ../CMakeLists.txt.metal ../CMakeLists.txt

# 配置和构建
echo "配置MacOS版本的AutoTalk (Metal版本)..."
cmake .. -DCMAKE_BUILD_TYPE=Release

echo "编译MacOS版本的AutoTalk (Metal版本)..."
cmake --build . --config Release

# 恢复原始CMakeLists.txt
cp ../CMakeLists.txt.original ../CMakeLists.txt

# 创建输出目录
mkdir -p ../output/bin
cp bin/autotalk ../output/bin/ 