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

# 复制可执行文件
if [ -f "build/Release/autotalk.exe" ]; then
    cp -f build/Release/autotalk.exe Release/
elif [ -f "build/autotalk.exe" ]; then
    cp -f build/autotalk.exe Release/
else
    echo "错误: 找不到可执行文件"
    exit 1
fi

./Release/autotalk.exe --mic 2 --model models/ggml-large-v3-turbo.bin