FROM nvidia/cuda:12.1.0-devel-ubuntu22.04

# 安装必要的依赖
RUN apt-get update && apt-get install -y \
    git \
    build-essential \
    cmake \
    wget \
    curl \
    zip \
    unzip \
    libasound2-dev \
    portaudio19-dev \
    libopenblas-dev \
    && rm -rf /var/lib/apt/lists/*

# 设置工作目录
WORKDIR /app

# 复制源代码
COPY . .

# 设置构建脚本权限并执行构建
RUN chmod +x build_autotalk_gpu.sh
CMD ["./build_autotalk_gpu.sh"] 