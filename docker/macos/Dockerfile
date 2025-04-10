FROM --platform=linux/amd64 ubuntu:22.04

# 安装必要的依赖
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    curl \
    wget \
    zip \
    unzip \
    libportaudio2 \
    portaudio19-dev \
    && rm -rf /var/lib/apt/lists/*

# 设置工作目录
WORKDIR /app

# 复制源代码
COPY . .

# 创建Mac专用构建脚本
RUN echo '#!/bin/bash' > build_mac_cpu.sh && \
    echo 'mkdir -p build_cpu' >> build_mac_cpu.sh && \
    echo 'cd build_cpu' >> build_mac_cpu.sh && \
    echo 'cmake .. -DCMAKE_BUILD_TYPE=Release -DUSE_CUDA=OFF -DGGML_CUDA=OFF' >> build_mac_cpu.sh && \
    echo 'cmake --build . --config Release' >> build_mac_cpu.sh && \
    echo 'mkdir -p /app/output/bin' >> build_mac_cpu.sh && \
    echo 'cp bin/autotalk /app/output/bin/' >> build_mac_cpu.sh && \
    chmod +x build_mac_cpu.sh

# 默认执行构建脚本
CMD ["./build_mac_cpu.sh"] 