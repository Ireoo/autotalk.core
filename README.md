# AutoTalk - 实时语音转文字工具

这是一个基于whisper.cpp库的实时语音转文字工具，可以实时捕获麦克风输入并将语音转换为文字。

## 功能特点

- 实时麦克风音频捕获
- 使用whisper.cpp进行高质量的语音识别
- 支持中文识别
- 低延迟处理
- 跨平台支持

## 系统要求

- C++17兼容的编译器
- CMake 3.14+
- PortAudio库
- 支持多线程

## 安装步骤

### 1. 克隆仓库

```bash
git clone https://github.com/yourusername/autotalk.git
cd autotalk
```

### 2. 安装依赖

#### Windows

在Windows上，需要安装PortAudio库。可以通过Chocolatey或MSYS2安装：

使用Chocolatey：
```bash
choco install portaudio
```

使用MSYS2：
```bash
pacman -S mingw-w64-x86_64-portaudio
```

#### Linux (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install -y portaudio19-dev
```

#### macOS

使用Homebrew：
```bash
brew install portaudio
```

### 3. 下载Whisper.cpp模型

项目提供了一个脚本来下载模型文件：

```bash
cd scripts
./download_model.sh small  # 可选: tiny, base, small, medium, large
cd ..
```

或者手动从[Whisper.cpp仓库](https://github.com/ggerganov/whisper.cpp)下载模型文件，并放置在`models`目录中。

推荐使用以下模型之一：
- `ggml-tiny.bin` - 最小模型，适合低配置设备
- `ggml-base.bin` - 平衡大小和准确性
- `ggml-small.bin` - 提供较好的准确性
- `ggml-medium.bin` - 高准确性模型

### 4. 构建项目

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

## 使用方法

```bash
./autotalk ../models/ggml-small.bin
```

运行程序后，它将开始捕获麦克风输入并实时转换为文字。按Ctrl+C退出程序。

## 自定义配置

你可以通过修改源代码中的常量来调整程序的行为：

- `SAMPLE_RATE` - 音频采样率（默认16kHz）
- `FRAME_SIZE` - 每帧的样本数（默认512）
- `MAX_BUFFER_SIZE` - 最大音频缓冲区大小（默认30秒）
- `AUDIO_CONTEXT_SIZE` - 音频上下文大小（默认3秒）

这些常量定义在`src/main.cpp`文件中：

```cpp
constexpr int SAMPLE_RATE = 16000;
constexpr int FRAME_SIZE = 512;
constexpr int MAX_BUFFER_SIZE = SAMPLE_RATE * 30; // 30秒的音频
constexpr int AUDIO_CONTEXT_SIZE = SAMPLE_RATE * 3; // 3秒的上下文
```

## 问题排查

1. 如果遇到音频设备初始化失败，请确保麦克风已正确连接并设置为默认输入设备。
2. 如果识别质量不佳，可以尝试使用更大的模型文件（medium或large）。
3. 对于高CPU使用率问题，可以在代码中降低以下值：
   ```cpp
   wparams.n_threads = 4;  // 减少线程数量以降低CPU使用率
   ```

## 开发

项目结构：
- `src/` - 源代码文件
- `include/` - 头文件
- `models/` - Whisper模型文件
- `scripts/` - 辅助脚本
- `whisper.cpp/` - Whisper.cpp库代码

## 许可证

本项目使用MIT许可证。详见LICENSE文件。

## 鸣谢

- [whisper.cpp](https://github.com/ggerganov/whisper.cpp) - OpenAI Whisper模型的C/C++实现
- [PortAudio](http://www.portaudio.com/) - 跨平台音频I/O库 