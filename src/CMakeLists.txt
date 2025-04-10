cmake_minimum_required(VERSION 3.18)
project(SimpleCudaTest CUDA CXX)

# 设置CUDA配置
set(CMAKE_CUDA_STANDARD 14)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 检查CUDA
include(CheckLanguage)
check_language(CUDA)

if(NOT CMAKE_CUDA_COMPILER)
    message(FATAL_ERROR "未找到CUDA编译器，请确保CUDA工具包已正确安装")
endif()

enable_language(CUDA)

# 获取CUDA架构
include(FindCUDA/select_compute_arch)
CUDA_DETECT_INSTALLED_GPUS(CUDA_GPUS)
string(STRIP "${CUDA_GPUS}" CUDA_GPUS)
string(REPLACE " " ";" CUDA_GPUS "${CUDA_GPUS}")
string(REPLACE "." "" CUDA_GPUS "${CUDA_GPUS}")
string(REGEX MATCHALL "[0-9]+" CUDA_GPUS "${CUDA_GPUS}")
list(SORT CUDA_GPUS)
list(REVERSE CUDA_GPUS)
list(GET CUDA_GPUS 0 CUDA_ARCH)
message(STATUS "自动检测到的CUDA架构: sm_${CUDA_ARCH}")

# 设置特定架构的CUDA标志
set(CMAKE_CUDA_ARCHITECTURES ${CUDA_ARCH})
set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -Wno-deprecated-gpu-targets")

# 添加可执行文件
add_executable(simple_cuda_test simple_cuda_test.cu)

# 链接CUDA库
target_link_libraries(simple_cuda_test PRIVATE ${CUDA_LIBRARIES})

# 设置编译选项
set_target_properties(simple_cuda_test PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    POSITION_INDEPENDENT_CODE ON
)

# 显示配置信息
message(STATUS "CUDA版本: ${CMAKE_CUDA_COMPILER_VERSION}")
message(STATUS "CUDA编译器: ${CMAKE_CUDA_COMPILER}")
message(STATUS "目标CUDA架构: ${CMAKE_CUDA_ARCHITECTURES}")

# 针对MinGW的特殊处理
if(MINGW)
    message(STATUS "检测到MinGW环境，进行相应配置")
    # 确保使用正确的链接器和库路径
    set_target_properties(simple_cuda_test PROPERTIES
        CUDA_RESOLVE_DEVICE_SYMBOLS ON
    )
endif() 