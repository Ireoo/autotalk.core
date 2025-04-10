cmake_minimum_required(VERSION 3.18)
project(CudaTestProject LANGUAGES CXX CUDA)

# 设置CMake策略
if(POLICY CMP0104)
  cmake_policy(SET CMP0104 NEW)
endif()

# 设置模块路径
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# 包含FindGPUArchitecture模块
include(FindGPUArchitecture)

# 检查CUDA可用性
include(CheckLanguage)
check_language(CUDA)

if(NOT CMAKE_CUDA_COMPILER)
    message(FATAL_ERROR "未找到CUDA编译器，请确保CUDA工具包已正确安装")
endif()

enable_language(CUDA)

# 查找CUDA包
find_package(CUDA REQUIRED)
include_directories(${CUDA_INCLUDE_DIRS})

# 检查CUDA版本
check_cuda_version()

# 直接设置CUDA架构
set(CMAKE_CUDA_ARCHITECTURES 75)
message(STATUS "设置CUDA架构: ${CMAKE_CUDA_ARCHITECTURES}")

# 设置CUDA标准
set(CMAKE_CUDA_STANDARD 14)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# 设置C++标准
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 添加whisper.cpp库路径
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/whisper.cpp)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/whisper.cpp/include)

# 定义CUDA支持宏
add_definitions(-DUSE_CUDA)

# 添加源文件
add_executable(simple_cuda_test src/simple_cuda_test.cu)
add_executable(cuda_test src/cuda_test.cpp)

# 设置编译属性
set_target_properties(simple_cuda_test PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    POSITION_INDEPENDENT_CODE ON
)

# 链接CUDA库
target_link_libraries(cuda_test PRIVATE ${CUDA_LIBRARIES})

# 输出项目信息
message(STATUS "========== CUDA测试项目配置 ==========")
message(STATUS "项目名称: ${PROJECT_NAME}")
message(STATUS "CMake版本: ${CMAKE_VERSION}")
message(STATUS "CUDA版本: ${CMAKE_CUDA_COMPILER_VERSION}")
message(STATUS "构建类型: ${CMAKE_BUILD_TYPE}")
message(STATUS "目标CUDA架构: ${CMAKE_CUDA_ARCHITECTURES}")
message(STATUS "CUDA库: ${CUDA_LIBRARIES}")
message(STATUS "=======================================") 