#!/bin/bash

# 这个脚本用于下载Whisper模型文件

MODEL_DIR="../models"
mkdir -p $MODEL_DIR

# 检查命令行参数
if [ "$#" -ne 1 ]; then
    echo "用法: $0 <模型大小: tiny|base|small|medium|large>"
    exit 1
fi

MODEL_SIZE=$1

# 根据模型大小选择合适的URL
case $MODEL_SIZE in
    "tiny")
        MODEL_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin"
        ;;
    "base")
        MODEL_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin"
        ;;
    "small")
        MODEL_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin"
        ;;
    "medium")
        MODEL_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium.bin"
        ;;
    "large")
        MODEL_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large.bin"
        ;;
    *)
        echo "错误: 不支持的模型大小 '$MODEL_SIZE'"
        echo "支持的选项: tiny, base, small, medium, large"
        exit 1
        ;;
esac

# 下载模型文件
MODEL_NAME="ggml-$MODEL_SIZE.bin"
MODEL_PATH="$MODEL_DIR/$MODEL_NAME"

echo "正在下载模型: $MODEL_NAME ..."
if command -v wget > /dev/null; then
    wget -O "$MODEL_PATH" "$MODEL_URL"
elif command -v curl > /dev/null; then
    curl -L -o "$MODEL_PATH" "$MODEL_URL"
else
    echo "错误: 需要安装wget或curl来下载模型"
    exit 1
fi

# 检查下载是否成功
if [ $? -eq 0 ]; then
    echo "模型下载成功: $MODEL_PATH"
else
    echo "模型下载失败"
    exit 1
fi

echo "现在你可以使用以下命令运行AutoTalk:"
echo "./autotalk \"$MODEL_PATH\"" 