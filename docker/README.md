# AutoTalk Docker构建环境

此目录包含用于在不同平台构建AutoTalk的Docker配置文件。

## 目录结构

- `linux/` - Linux平台构建环境 (CUDA加速)
- `windows/` - Windows平台构建环境 (CUDA加速)
- `macos/` - macOS平台构建环境 (CPU/Metal加速)
- `download_models.sh` - 下载Whisper模型的脚本

## 手动构建说明

### Linux平台

```bash
# 构建Docker镜像
docker build -t autotalk-linux -f docker/linux/Dockerfile .

# 运行构建
mkdir -p build_output/linux
docker run --rm -v $(pwd)/build_output/linux:/app/build_autotalk_gpu/bin autotalk-linux
```

### Windows平台

```powershell
# 构建Docker镜像
docker build -t autotalk-windows -f docker/windows/Dockerfile .

# 运行构建
mkdir -p build_output/windows
docker run --rm -v ${PWD}/build_output/windows:C:\app\build_autotalk_gpu\bin\Release autotalk-windows
```

### macOS平台

```bash
# 构建Docker镜像 (CPU版本)
docker build -t autotalk-macos -f docker/macos/Dockerfile .

# 运行构建 (CPU版本)
mkdir -p build_output/macos
docker run --rm -v $(pwd)/build_output/macos:/app/output/bin autotalk-macos

# 构建Metal版本 (本地)
chmod +x docker/macos/build_mac_metal.sh
./docker/macos/build_mac_metal.sh
```

## 模型下载

```bash
chmod +x docker/download_models.sh
./docker/download_models.sh
```

## 使用GitHub Actions自动构建

本项目配置了GitHub Actions工作流，可以自动构建所有平台的版本。查看`.github/workflows/docker_build.yml`文件了解详情。

工作流程会：
1. 为每个平台构建Docker镜像
2. 在Docker容器中编译AutoTalk
3. 下载Whisper模型
4. 打包所有平台的构建产物和模型 