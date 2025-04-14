#!/bin/bash

# 设置错误时退出
set -e

echo "==== 开始构建项目 ===="

# 显示当前工作目录
echo "当前工作目录: $(pwd)"

# 检查并下载依赖项
if [ ! -d "portaudio" ]; then
    echo "正在下载PortAudio..."
    git clone https://github.com/PortAudio/portaudio.git
fi

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

# 检查并下载Boost库
if [ ! -d "third_party/boost" ]; then
    echo "正在下载Boost库..."
    mkdir -p third_party
    curl -L https://github.com/boostorg/boost/releases/download/boost-1.88.0/boost-1.88.0-b2-nodocs.tar.gz -o third_party/boost.tar.gz
    tar -xzf third_party/boost.tar.gz -C third_party
    mv third_party/boost-1.88.0 third_party/boost
    rm third_party/boost.tar.gz
fi

# 构建Boost库
cd third_party/boost
echo "正在构建Boost库..."
./bootstrap.sh --with-libraries=atomic,thread,system,filesystem,regex,date_time,chrono
./b2 headers
./b2 install --prefix=../install --with-atomic --with-thread --with-system --with-filesystem --with-regex --with-date_time --with-chrono
cd ../..

# 构建 PortAudio
echo "正在构建 PortAudio..."
cd portaudio
sed -i 's/cmake_minimum_required(VERSION 3.1.0)/cmake_minimum_required(VERSION 3.5.0)/g' CMakeLists.txt
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DPA_BUILD_SHARED_LIBS=ON
make
cd ../..

# 构建 libsndfile
echo "正在构建 libsndfile..."
cd third_party/libsndfile
mkdir -p build
cd build
cmake .. -DBUILD_SHARED_LIBS=ON
make
cd ../../..

mkdir -p build
cd build

# 设置Boost路径
BOOST_ROOT="$(pwd)/../third_party/boost"
BOOST_INCLUDEDIR="$(pwd)/../third_party/boost/install/include"
BOOST_LIBRARYDIR="$(pwd)/../third_party/boost/install/lib"

echo "Boost路径信息："
echo "BOOST_ROOT: ${BOOST_ROOT}"
echo "BOOST_INCLUDEDIR: ${BOOST_INCLUDEDIR}"
echo "BOOST_LIBRARYDIR: ${BOOST_LIBRARYDIR}"

cmake -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SHARED_LIBS=ON \
      -DBOOST_ROOT="${BOOST_ROOT}" \
      -DBOOST_INCLUDEDIR="${BOOST_INCLUDEDIR}" \
      -DBOOST_LIBRARYDIR="${BOOST_LIBRARYDIR}" \
      -DPortAudio_DIR="$(pwd)/../portaudio/install/lib/cmake/portaudio" \
      -DCMAKE_PREFIX_PATH="$(pwd)/../portaudio/install" \
      ..

make

cd ..

# 创建Release目录
rm -rf Release
mkdir -p Release

# 复制可执行文件
if [ -f "build/autotalk" ]; then
    cp -f build/autotalk Release/
else
    echo "错误: 找不到可执行文件"
    exit 1
fi

# 复制共享库文件
echo "正在复制共享库文件..."

# 复制PortAudio共享库
if [ -f "portaudio/build/libportaudio.so" ]; then
    cp -f portaudio/build/libportaudio.so* Release/
else
    echo "错误: 找不到libportaudio.so"
    exit 1
fi

# 复制libsndfile共享库
if [ -f "third_party/libsndfile/build/libsndfile.so" ]; then
    cp -f third_party/libsndfile/build/libsndfile.so* Release/
else
    echo "错误: 找不到libsndfile.so"
    exit 1
fi

# 复制whisper共享库
if [ -f "build/whisper.cpp/src/libwhisper.so" ]; then
    cp -f build/whisper.cpp/src/libwhisper.so* Release/
    cp -f build/whisper.cpp/ggml/src/libggml.so* Release/
    cp -f build/whisper.cpp/ggml/src/libggml-cpu.so* Release/
    cp -f build/whisper.cpp/ggml/src/libggml-base.so* Release/
else
    echo "错误: 找不到whisper共享库"
    exit 1
fi

echo "构建完成！"
echo "==== 构建完成 ===="
echo "可执行文件位于 Release 目录中"

# 运行程序
# ./Release/autotalk --list 