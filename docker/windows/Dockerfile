FROM nvidia/cuda:12.1.0-devel-windowsservercore-2022

# 设置PowerShell作为Shell
SHELL ["powershell", "-Command", "$ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue';"]

# 安装必要的工具和依赖
RUN choco install -y cmake git visualstudio2022buildtools visualstudio2022-workload-vctools

# 创建工作目录
WORKDIR C:\\app

# 复制源代码
COPY . .

# 创建构建脚本
RUN Set-Content -Path build_autotalk_gpu.bat -Value "@echo off`r`nmkdir build_autotalk_gpu`r`ncd build_autotalk_gpu`r`ncmake .. -DCMAKE_BUILD_TYPE=Release -DUSE_CUDA=ON -DGGML_CUDA=ON`r`ncmake --build . --config Release"

# 默认执行构建脚本
CMD ["cmd", "/c", "build_autotalk_gpu.bat"] 