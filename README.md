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

#### Windows (使用vcpkg)

```bash
vcpkg install portaudio
```

#### Linux (Ubuntu/Debian)

```bash
sudo apt-get install libportaudio2 libportaudio-dev
```

#### macOS

```bash
brew install portaudio
```

### 3. 下载Whisper模型

从[Whisper.cpp仓库](https://github.com/ggerganov/whisper.cpp)下载模型文件，并放置在`models`目录中。

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
./autotalk /path/to/model/ggml-small.bin
```

运行程序后，它将开始捕获麦克风输入并实时转换为文字。按Ctrl+C退出程序。

## 自定义配置

你可以通过修改源代码中的常量来调整程序的行为：

- `SAMPLE_RATE` - 音频采样率（默认16kHz）
- `FRAME_SIZE` - 每帧的样本数（默认512）
- `MAX_BUFFER_SIZE` - 最大音频缓冲区大小（默认30秒）
- `AUDIO_CONTEXT_SIZE` - 音频上下文大小（默认3秒）

## 问题排查

1. 如果遇到音频设备初始化失败，请确保麦克风已正确连接并设置为默认输入设备。
2. 如果识别质量不佳，可以尝试使用更大的模型文件。
3. 对于高CPU使用率问题，可以在代码中降低`wparams.n_threads`的值。

## 许可证

本项目使用MIT许可证。详见LICENSE文件。

## 鸣谢

- [whisper.cpp](https://github.com/ggerganov/whisper.cpp) - OpenAI Whisper模型的C/C++实现
- [PortAudio](http://www.portaudio.com/) - 跨平台音频I/O库 