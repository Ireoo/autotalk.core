cmake_minimum_required(VERSION 3.10)
project(AutoTalk LANGUAGES CXX)

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

# 如果启用GPU，添加CUDA语言支持
if(USE_GPU)
    message(STATUS "GPU支持已启用")
    
    # 设置CUDA策略，避免新旧版本不兼容问题
    if(POLICY CMP0104)
        cmake_policy(SET CMP0104 NEW)
    endif()
    
    # 设置CUDA架构
    set(CMAKE_CUDA_ARCHITECTURES "all-major")
    message(STATUS "CUDA架构设置为: all-major")
    
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
    
    # 设置CUDA相关参数，确保和Whisper.cpp使用相同设置
    set(GGML_CUDA ON)
    
    # 添加NVAPI支持
    if(WIN32)
        # 尝试寻找NVAPI SDK的路径
        if(EXISTS "$ENV{NVAPI_SDK_PATH}")
            set(NVAPI_SDK_PATH "$ENV{NVAPI_SDK_PATH}")
        else()
            # 尝试在标准位置查找NVAPI SDK
            set(NVAPI_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nvapi")
        endif()
        
        # 检查NVAPI SDK是否存在
        if(EXISTS "${NVAPI_SDK_PATH}/nvapi.h")
            message(STATUS "找到NVAPI SDK: ${NVAPI_SDK_PATH}")
            include_directories(${NVAPI_SDK_PATH})
            add_definitions(-DHAVE_NVAPI)
            
            # 根据架构选择正确的库
            if(CMAKE_SIZEOF_VOID_P EQUAL 8)
                set(NVAPI_LIB "${NVAPI_SDK_PATH}/amd64/nvapi64.lib")
            else()
                set(NVAPI_LIB "${NVAPI_SDK_PATH}/x86/nvapi.lib")
            endif()
            
            # 检查库文件是否存在
            if(EXISTS "${NVAPI_LIB}")
                message(STATUS "找到NVAPI库: ${NVAPI_LIB}")
                # 添加HAVE_NVAPI定义
                add_definitions(-DHAVE_NVAPI)
                set(HAVE_NVAPI TRUE)
                message(STATUS "启用HAVE_NVAPI定义")
            else()
                message(WARNING "未找到NVAPI库: ${NVAPI_LIB}")
                set(NVAPI_LIB "")
            endif()
        else()
            message(WARNING "未找到NVAPI SDK，GPU温度和内存监控功能可能不可用")
        endif()
    endif()
endif()

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

# 设置whisper.cpp相关选项
if(USE_GPU)
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

# 添加whisper.cpp作为子目录
add_subdirectory(whisper.cpp)

# 查找并链接Threads库
find_package(Threads REQUIRED)

# 包含头文件目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/whisper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libsndfile/include
)

# 如果启用GPU，添加CUDA头文件目录
if(USE_GPU)
    include_directories(${CUDA_INCLUDE_DIRS})
endif()

# 添加系统监控源文件
set(MONITORING_SOURCES
    src/system_monitor.cpp
)

# 添加socket.io服务器源文件
set(SOCKETIO_SOURCES
    src/audio_server.cpp
)

# 添加主程序源文件
add_executable(autotalk 
    src/main.cpp
    ${MONITORING_SOURCES}
    ${SOCKETIO_SOURCES}
)

# 设置编译选项
if(MSVC)
    target_compile_options(autotalk PRIVATE /utf-8 /EHsc)
    add_definitions(-D_WIN32_WINNT=0x0601)
endif()

# 链接库
target_link_libraries(autotalk PRIVATE
    whisper
    sndfile
    ${CMAKE_THREAD_LIBS_INIT}
)

# Windows平台特定链接
if(WIN32)
    target_link_libraries(autotalk PRIVATE 
        pdh 
        ws2_32 
        iphlpapi
    )
endif()

# 如果启用GPU，链接CUDA库
if(USE_GPU)
    target_link_libraries(autotalk PRIVATE ${CUDA_LIBRARIES})
    
    # 如果找到了NVAPI库，链接它
    if(WIN32 AND DEFINED NVAPI_LIB AND EXISTS "${NVAPI_LIB}")
        target_link_libraries(autotalk PRIVATE ${NVAPI_LIB})
        # 添加NVAPI相关的编译宏
        if(HAVE_NVAPI)
            target_compile_definitions(autotalk PRIVATE HAVE_NVAPI)
        endif()
    endif()
endif()

# 复制模型目录
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/models)

# 输出构建配置摘要
message(STATUS "--------- 构建配置摘要 ---------")
message(STATUS "构建类型: ${CMAKE_BUILD_TYPE}")
message(STATUS "GPU支持: ${USE_GPU}")
if(USE_GPU)
    message(STATUS "CUDA版本: ${CUDA_VERSION}")
endif()
message(STATUS "-------------------------------") 