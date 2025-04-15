cmake_minimum_required(VERSION 3.10)
project(AutoTalk)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置vcpkg工具链
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")

# 设置构建共享库
set(BUILD_SHARED_LIBS ON)

# 添加cmake模块路径
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

# libsndfile 配置
set(BUILD_PROGRAMS OFF CACHE BOOL "Build programs")
set(BUILD_EXAMPLES OFF CACHE BOOL "Build examples")
set(BUILD_TESTING OFF CACHE BOOL "Build tests")
set(ENABLE_EXTERNAL_LIBS OFF CACHE BOOL "Enable external libs")
set(ENABLE_MPEG OFF CACHE BOOL "Enable MPEG support")

# 添加 libsndfile 作为子项目
add_subdirectory(third_party/libsndfile)

# 设置PortAudio编译选项
option(PA_BUILD_SHARED "Build PortAudio shared library" ON)
option(PA_BUILD_STATIC "Build PortAudio static library" OFF)
option(PA_BUILD_EXAMPLES "Build PortAudio examples" OFF)
option(PA_BUILD_TESTS "Build PortAudio tests" OFF)
option(PA_DLL_LINK_WITH_STATIC_RUNTIME "Link PortAudio DLL with static runtime libraries" OFF)
option(PA_ENABLE_DEBUG_OUTPUT "Enable debug output for PortAudio" OFF)

# 添加PortAudio作为子项目
add_subdirectory(portaudio)

# 添加whisper.cpp作为子目录
add_subdirectory(whisper.cpp)

# 包含头文件目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/whisper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/portaudio/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libsndfile/include
)

# 添加系统监控源文件
set(MONITORING_SOURCES
    src/system_monitor.cpp
)

# 添加主程序源文件
add_executable(autotalk 
    src/main.cpp
    src/audio_capture.cpp
    ${MONITORING_SOURCES}
)

# 设置编译选项
if(MSVC)
    target_compile_options(autotalk PRIVATE /utf-8 /EHsc)
endif()

# 链接库
target_link_libraries(autotalk PRIVATE
    portaudio
    whisper
    sndfile
)

# Windows特定链接
if(WIN32)
    target_link_libraries(autotalk PRIVATE pdh)
endif()

# 复制模型目录
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/models) 