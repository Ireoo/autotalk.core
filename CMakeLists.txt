cmake_minimum_required(VERSION 3.5)
project(AutoTalk LANGUAGES CXX)

# 平台检测
if(WIN32)
    message(STATUS "Windows平台")
    add_definitions(-D_WIN32)
elseif(UNIX AND NOT APPLE)
    message(STATUS "Linux平台")
    add_definitions(-DPLATFORM_LINUX)
elseif(APPLE)
    message(STATUS "MacOS平台")
    add_definitions(-DPLATFORM_MACOS)
endif()

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置CMake策略
cmake_policy(SET CMP0077 NEW)

# 输出调试信息
message(STATUS "CMAKE_VERSION: ${CMAKE_VERSION}")
message(STATUS "CMAKE_GENERATOR: ${CMAKE_GENERATOR}")

# 检查是否启用GPU
option(USE_GPU "Enable GPU support" OFF)
option(GGML_METAL "Enable Metal GPU support on macOS" OFF)

# 如果启用GPU，添加相应的硬件加速支持
if(USE_GPU)
    message(STATUS "GPU支持已启用")
    
    if(APPLE)
        # macOS上使用Metal
        message(STATUS "在macOS上使用Metal GPU加速")
        set(GGML_METAL ON CACHE BOOL "Enable Metal support" FORCE)
        set(WHISPER_METAL ON CACHE BOOL "Enable Metal for Whisper" FORCE)
        add_definitions(-DGGML_METAL)
        add_definitions(-DWHISPER_METAL)
    else()
        # 非macOS平台使用CUDA
        message(STATUS "使用CUDA GPU加速")
        
        # 设置CUDA策略，避免新旧版本不兼容问题
        if(POLICY CMP0104)
            cmake_policy(SET CMP0104 NEW)
        endif()
        
        # 设置CUDA架构
        # set(CMAKE_CUDA_ARCHITECTURES "all-major")
        # message(STATUS "CUDA架构设置为: all-major")
        
        # 添加CUDA语言支持
        enable_language(CUDA)
        
        # 查找CUDA包
        find_package(CUDA REQUIRED)
        if(CUDA_FOUND)
            message(STATUS "找到CUDA: ${CUDA_VERSION}")
            message(STATUS "CUDA库: ${CUDA_LIBRARIES}")
            message(STATUS "CUDA包含目录: ${CUDA_INCLUDE_DIRS}")
            message(STATUS "CUDA工具包根目录: ${CUDA_TOOLKIT_ROOT_DIR}")
        else()
            message(FATAL_ERROR "未找到CUDA")
        endif()
        
        # 添加CUDA定义
        add_definitions(-DUSE_CUDA)
        
        # 设置CUDA相关参数
        set(GGML_CUDA ON CACHE BOOL "Enable CUDA" FORCE)
        set(GGML_CUBLAS ON CACHE BOOL "Enable CUBLAS" FORCE)
        
        # 确保whisper.cpp能找到CUDA
        if(DEFINED ENV{CUDA_PATH})
            set(CUDA_BIN_PATH "$ENV{CUDA_PATH}/bin" CACHE PATH "CUDA binary path")
            message(STATUS "使用环境变量CUDA_PATH: $ENV{CUDA_PATH}")
        elseif(DEFINED ENV{CUDA_HOME})
            set(CUDA_BIN_PATH "$ENV{CUDA_HOME}/bin" CACHE PATH "CUDA binary path")
            message(STATUS "使用环境变量CUDA_HOME: $ENV{CUDA_HOME}")
        endif()
    endif()
endif()

# 设置vcpkg工具链
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")

# 设置构建共享库
set(BUILD_SHARED_LIBS ON)

# 添加cmake模块路径
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

# 设置 libsndfile 配置
set(BUILD_PROGRAMS OFF CACHE BOOL "Build programs")
set(BUILD_EXAMPLES OFF CACHE BOOL "Build examples")
set(BUILD_TESTING OFF CACHE BOOL "Build tests")
set(ENABLE_EXTERNAL_LIBS OFF CACHE BOOL "Enable external libs")
set(ENABLE_MPEG OFF CACHE BOOL "Enable MPEG support")

# 添加 libsndfile 作为子项目
add_subdirectory(third_party/libsndfile)

# 下载并配置nlohmann_json库
include(FetchContent)
FetchContent_Declare(
  json
  URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
FetchContent_MakeAvailable(json)

# 设置whisper.cpp相关选项
if(USE_GPU)
    if(APPLE)
        # macOS上使用Metal
        set(WHISPER_METAL ON CACHE BOOL "Enable Metal for Whisper" FORCE)
    else()
        # 非macOS平台使用CUDA
        # 使用新的选项替代已弃用的WHISPER_CUBLAS
        set(GGML_CUDA ON CACHE BOOL "Enable CUDA" FORCE)
        set(GGML_CUBLAS ON CACHE BOOL "Enable CUBLAS" FORCE)
        
        # 确保whisper.cpp能找到CUDA
        if(DEFINED ENV{CUDA_PATH})
            set(CUDA_BIN_PATH "$ENV{CUDA_PATH}/bin" CACHE PATH "CUDA binary path")
            message(STATUS "使用环境变量CUDA_PATH: $ENV{CUDA_PATH}")
        elseif(DEFINED ENV{CUDA_HOME})
            set(CUDA_BIN_PATH "$ENV{CUDA_HOME}/bin" CACHE PATH "CUDA binary path")
            message(STATUS "使用环境变量CUDA_HOME: $ENV{CUDA_HOME}")
        endif()
    endif()
endif()

# 添加whisper.cpp作为子目录
add_subdirectory(whisper.cpp)

# 包含头文件目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/whisper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libsndfile/include
    ${json_SOURCE_DIR}/include
)

# 如果启用GPU，添加相应平台的头文件目录
if(USE_GPU)
    if(APPLE)
        # 添加Metal相关头文件
        # Metal framework automatically included on macOS
    else()
        # 添加CUDA头文件目录
        include_directories(${CUDA_INCLUDE_DIRS})
    endif()
endif()

# 添加系统监控源文件
set(MONITORING_SOURCES
    src/system_monitor.cpp
)

# 添加主程序源文件
add_executable(autotalk 
    src/main.cpp
    src/audio_server.cpp
    src/websocket_server.cpp
    src/voiceprint_recognition.cpp
    ${MONITORING_SOURCES}
)

# 设置编译选项
if(MSVC)
    target_compile_options(autotalk PRIVATE /utf-8 /EHsc)
endif()

# 定义预处理器宏
add_definitions(-D_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)

# 链接库
target_link_libraries(autotalk PRIVATE
    whisper
    sndfile
)

# 如果启用GPU，链接相应的库
if(USE_GPU)
    if(NOT APPLE)
        target_link_libraries(autotalk PRIVATE ${CUDA_LIBRARIES})
    endif()
endif()

# Windows特定链接
if(WIN32)
    target_link_libraries(autotalk PRIVATE pdh ws2_32 iphlpapi)
endif()

# 复制模型目录
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/models)

# 输出构建配置摘要
message(STATUS "--------- 构建配置摘要 ---------")
message(STATUS "构建类型: ${CMAKE_BUILD_TYPE}")
message(STATUS "GPU支持: ${USE_GPU}")
if(USE_GPU)
    if(APPLE)
        message(STATUS "GPU类型: Metal")
    else()
        message(STATUS "GPU类型: CUDA")
        message(STATUS "CUDA版本: ${CUDA_VERSION}")
    endif()
endif()
message(STATUS "-------------------------------") 