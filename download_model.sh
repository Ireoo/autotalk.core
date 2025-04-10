#!/bin/bash

# 创建模型目录
mkdir -p models

# 模型列表
MODEL_TYPES=("tiny" "base" "small" "medium" "large")

# 显示帮助
show_help() {
    echo "用法: $0 [模型类型]"
    echo "可用的模型类型:"
    echo "  tiny   - 最小模型 (~75MB)"
    echo "  base   - 基本模型 (~142MB)"
    echo "  small  - 小型模型 (~466MB)"
    echo "  medium - 中型模型 (~1.5GB)"
    echo "  large  - 大型模型 (~3GB)"
    echo ""
    echo "示例: $0 small"
}

# 检查参数
if [ "$#" -lt 1 ]; then
    show_help
    exit 1
fi

MODEL=$1

# 验证模型类型
VALID_MODEL=false
for type in "${MODEL_TYPES[@]}"; do
    if [ "$MODEL" = "$type" ]; then
        VALID_MODEL=true
        break
    fi
done

if [ "$VALID_MODEL" = false ]; then
    echo "错误: 无效的模型类型 '$MODEL'"
    show_help
    exit 1
fi

# 下载模型
echo "正在下载 $MODEL 模型..."
MODEL_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-$MODEL.bin"
OUTPUT_FILE="models/ggml-$MODEL.bin"

if command -v wget &> /dev/null; then
    wget -O "$OUTPUT_FILE" "$MODEL_URL"
elif command -v curl &> /dev/null; then
    curl -L -o "$OUTPUT_FILE" "$MODEL_URL"
else
    echo "错误: 需要wget或curl来下载模型"
    exit 1
fi

# 验证下载
if [ -f "$OUTPUT_FILE" ]; then
    echo "模型已成功下载到 $OUTPUT_FILE"
    echo "您现在可以运行: ./autotalk $OUTPUT_FILE"
else
    echo "错误: 模型下载失败"
    exit 1
fi 