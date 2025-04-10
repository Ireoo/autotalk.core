cmake_minimum_required(VERSION 3.18)
project(cuda_simple_test CUDA CXX)

# 设置CMake模块路径
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/../cmake)

# 寻找CUDA
find_package(CUDA REQUIRED)

# 确保找到CUDA
if(NOT CUDA_FOUND)
  message(FATAL_ERROR "CUDA not found!")
endif()

# 包含GPU架构检测模块
include(FindGPUArchitecture)

# 设置CUDA标准
set(CMAKE_CUDA_STANDARD 14)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# 检查CUDA版本
check_cuda_version()

# 检测GPU架构并设置相应的编译标志
set_cuda_arch_flags()

# 添加可执行文件
add_executable(cuda_test simple_cuda_test.cu)

# 目标属性设置
set_target_properties(cuda_test PROPERTIES
  CUDA_SEPARABLE_COMPILATION ON
  POSITION_INDEPENDENT_CODE ON
)

# 设置包含目录
target_include_directories(cuda_test PRIVATE ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES})

# 链接CUDA库
target_link_libraries(cuda_test PRIVATE ${CUDA_LIBRARIES})

# 安装目标
install(TARGETS cuda_test DESTINATION bin)

# 打印信息
message(STATUS "CUDA compiler: ${CMAKE_CUDA_COMPILER}")
message(STATUS "CUDA version: ${CUDA_VERSION}")
message(STATUS "CUDA architecture flags: ${CMAKE_CUDA_FLAGS}") 