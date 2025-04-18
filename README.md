# AutoTalk 音频实时识别系统

AutoTalk是一个基于Whisper和Socket.IO的实时语音识别系统，支持实时音频流处理和语音识别。系统包含服务端和桌面客户端两部分。

## 功能特性

- 基于Whisper的高精度语音识别
- 使用Socket.IO进行实时音频数据传输
- 同时支持实时识别结果和完整句子识别结果
- 支持GPU加速（可选）
- 跨平台桌面客户端应用

## 系统要求

- Windows/Linux/macOS
- C++17兼容的编译器
- CMake 3.10+
- Node.js和npm（用于客户端开发）

## 快速开始

### 编译服务端

使用build.sh脚本一键编译服务端：

```bash
# 使用CPU版本编译
./build.sh

# 使用GPU加速编译（需要CUDA）
./build.sh --gpu

# 跳过依赖项下载
./build.sh --skip-deps
```

### 运行服务端

编译完成后，可以通过以下命令运行服务端：

```bash
./Release/autotalk.exe --port 3000 --model models/ggml-base.bin
```

参数说明：
- `--port <端口号>`: 指定服务器监听端口（默认：3000）
- `--model <模型路径>`: 指定使用的Whisper模型路径
- `--list`: 列出已安装的模型文件
- `--help`: 显示帮助信息

### 编译和运行客户端

客户端基于Electron和Vue.js开发，使用以下命令启动：

```bash
cd client
npm install         # 安装依赖
npm run electron:dev  # 开发模式启动
npm run electron:build # 构建客户端应用
```

## 项目结构

```
autotalk/
├── build.sh                # 构建脚本
├── CMakeLists.txt          # CMake配置文件
├── include/                # 头文件目录
│   ├── audio_server.h      # 音频服务器接口
│   └── system_monitor.h    # 系统监控接口
├── src/                    # 源代码目录
│   ├── main.cpp            # 主程序入口
│   ├── audio_server.cpp    # 音频服务器实现
│   └── system_monitor.cpp  # 系统监控实现
├── client/                 # 客户端应用
│   ├── package.json        # 客户端依赖配置
│   ├── electron/           # Electron主进程代码
│   └── src/                # 客户端源代码
├── models/                 # Whisper模型目录
└── third_party/            # 第三方库
    ├── socket.io-cpp/      # Socket.IO C++库
    └── libsndfile/         # 音频文件处理库
```

## 使用说明

1. 启动服务端，确保指定了正确的端口和模型路径
2. 启动客户端应用
3. 在客户端中输入服务端地址（例如：http://localhost:3000）
4. 点击"连接服务器"按钮
5. 选择音频输入设备
6. 点击"开始识别"按钮开始实时语音识别
7. 识别结果将实时显示在界面上

## Socket.IO接口说明

服务端提供以下Socket.IO事件：

- `server_ready`: 服务器准备就绪通知
- `recognition_result`: 发送识别结果（带前缀L:或T:）

客户端提供以下Socket.IO事件：

- `audio_data`: 发送音频数据到服务器

## 识别结果格式

- `L:文本内容`: 实时识别的临时结果
- `T:文本内容`: 识别的完整句子结果

## 许可证

本项目采用MIT许可证。查看LICENSE文件获取更多信息。 