cmake_minimum_required(VERSION 3.10)
project(nvapi)

# 创建mock NVAPI静态库
add_library(nvapi64 STATIC nvapi.cpp)

# 添加必要的库依赖
target_link_libraries(nvapi64 PUBLIC
    ole32
    oleaut32
    wbemuuid
)

# 添加包含目录
target_include_directories(nvapi64 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 指定输出目录
set_target_properties(nvapi64 PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/amd64"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/amd64"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/amd64"
)

# 将头文件复制到输出目录
configure_file(nvapi.h ${CMAKE_CURRENT_SOURCE_DIR}/amd64/nvapi.h COPYONLY) 