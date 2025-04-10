#!/bin/bash

# 设置错误处理
set -e

# 创建模型目录
mkdir -p models

echo "开始下载Whisper模型..."

# 下载小型中文模型
echo "下载小型中文模型(ggml-small.bin)..."
curl -L "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin" -o models/ggml-small.bin

# 如果需要下载更多或其他模型，可以在此处添加
# 例如：
# echo "下载tiny模型..."
# curl -L "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin" -o models/ggml-tiny.bin

# 下载完成
echo "模型下载完成! 模型保存在 models/ 目录中。"
echo "可用模型:"
ls -lh models/ 