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

echo "系统类型: $OSTYPE"

# 构建Boost库
cd third_party/boost
echo "正在构建Boost库..."
if [[ "$OSTYPE" == "linux-gnu" || "$OSTYPE" == "darwin" || "$OSTYPE" == "darwin23" ]]; then
    ./bootstrap.sh --with-libraries=atomic,thread,system,filesystem,regex,date_time,chrono
    ./b2 install --prefix=../install
elif [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" ]]; then
    cmd.exe //c "bootstrap.bat"
    cmd.exe //c "b2.exe install --prefix=../install --with-atomic --with-thread --with-system --with-filesystem --with-regex --with-date_time --with-chrono toolset=msvc-14.3 architecture=x86 address-model=64 link=shared runtime-link=shared variant=release"
fi
cd ../..

# 构建 PortAudio
echo "正在构建 PortAudio..."
cd portaudio
if [[ "$OSTYPE" == "linux-gnu" || "$OSTYPE" == "darwin" || "$OSTYPE" == "darwin23" ]]; then
    ./configure
    make
elif [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" ]]; then
    mkdir -p build
    cd build
    cmake -G "Visual Studio 17 2022" -A x64 -DPA_BUILD_SHARED=ON ..
    cmake --build . --config Release
    cd ..
fi
cd ..


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

cmake --build . --config Release

cd ..

# 创建Release目录
rm -rf Release
mkdir -p Release

# 复制可执行文件
if [ -f "build/Release/autotalk.exe" ]; then
    cp -f build/Release/autotalk.exe Release/
elif [ -f "build/autotalk.exe" ]; then
    cp -f build/autotalk.exe Release/
else
    echo "错误: 找不到可执行文件"
    exit 1
fi



# 复制Boost DLL文件
cp -f third_party/install/lib/*.dll Release/

# 复制模型文件
cp -f models/*.bin Release/

echo "正在构建 libsndfile..."

# 进入 libsndfile 目录
cd third_party/libsndfile || exit 1

# 清理并创建构建目录
rm -rf build
mkdir build
cd build || exit 1

# 配置并构建
cmake .. -G "Visual Studio 17 2022" -A x64 -DBUILD_SHARED_LIBS=ON
cmake --build . --config Release

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

# 复制必要的DLL文件
echo "正在复制DLL文件..."

# 复制PortAudio DLL
if [ -f "build/portaudio/Release/portaudio.dll" ]; then
    cp -f build/portaudio/Release/portaudio.dll Release/
elif [ -f "build/portaudio/Debug/portaudio.dll" ]; then
    cp -f build/portaudio/Debug/portaudio.dll Release/
else
    echo "错误: 找不到portaudio.dll"
    exit 1
fi

# 复制whisper和其他必要的DLL文件
if [ -f "build/bin/Release/whisper.dll" ]; then
    cp -f build/bin/Release/whisper.dll Release/
    cp -f build/bin/Release/ggml.dll Release/
    cp -f build/bin/Release/ggml-cpu.dll Release/
    cp -f build/bin/Release/ggml-base.dll Release/
else
    echo "错误: 找不到whisper.dll"
    exit 1
fi

echo "构建完成！"

echo "==== 构建完成 ===="
echo "可执行文件位于 Release 目录中"# 运行程序

./Release/autotalk.exe --list
