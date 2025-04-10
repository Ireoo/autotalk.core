cmake_minimum_required(VERSION 3.14)
project(AutoTalk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加自定义CMake模块路径
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules)

# 定义GPU相关选项
option(USE_CUDA "使用CUDA加速" ON)
option(AUTO_DETECT_GPU "自动检测GPU架构" ON)

# 如果启用CUDA，添加CUDA语言支持
if(USE_CUDA)
  enable_language(CUDA)
  
  # 获取CUDA版本信息
  if(CMAKE_CUDA_COMPILER_VERSION VERSION_GREATER_EQUAL 11.0)
    message(STATUS "CUDA版本 >= 11.0，不支持compute_30/35/37架构")
    set(DEFAULT_CUDA_ARCHITECTURES "50;52;53;60;61;62;70;72;75;80;86")
  else()
    message(STATUS "CUDA版本 < 11.0，支持所有架构")
    set(DEFAULT_CUDA_ARCHITECTURES "30;35;37;50;52;53;60;61;62;70;72;75")
  endif()
  
  # 自动检测GPU架构
  if(AUTO_DETECT_GPU)
    message(STATUS "自动检测可用的GPU架构...")
    include(FindGPUArchitecture)
    detect_gpu_architectures(DETECTED_CUDA_ARCHITECTURES)
    if(DETECTED_CUDA_ARCHITECTURES)
      set(CUDA_ARCHITECTURES ${DETECTED_CUDA_ARCHITECTURES})
    else()
      set(CUDA_ARCHITECTURES ${DEFAULT_CUDA_ARCHITECTURES})
    endif()
  else()
    set(CUDA_ARCHITECTURES ${DEFAULT_CUDA_ARCHITECTURES})
  endif()
  
  message(STATUS "使用GPU架构: ${CUDA_ARCHITECTURES}")
  
  # 设置CUDA编译选项
  set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -DUSE_CUDA")
  
  # 设置目标CUDA架构
  set(CMAKE_CUDA_ARCHITECTURES ${CUDA_ARCHITECTURES})
endif()

# 设置PortAudio路径
set(PORTAUDIO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/portaudio)
set(PORTAUDIO_INCLUDE_DIRS ${PORTAUDIO_DIR}/include)
set(PORTAUDIO_LIBRARIES ${PORTAUDIO_DIR}/build/Release/portaudio.lib)

# 忽略警告
if(MSVC)
  add_compile_options(/wd4267 /wd4244 /wd4996 /wd4819 /utf-8)
endif()

# 添加whisper.cpp库
add_subdirectory(whisper.cpp EXCLUDE_FROM_ALL)

# 查找必要的库
find_package(Threads REQUIRED)

# 包含头文件目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/whisper.cpp
    ${PORTAUDIO_INCLUDE_DIRS}
)

# 添加可执行文件
add_executable(autotalk
    src/main_simple.cpp
    src/audio_recorder.cpp
)

# 链接库
target_link_libraries(autotalk
    PRIVATE
    whisper
    ${PORTAUDIO_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
)

# 复制模型文件到输出目录
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/models/ DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/models/)

# 添加模型测试程序
add_executable(model_test src/model_test.cpp)
target_include_directories(model_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(model_test PRIVATE whisper) 