#include <iostream>
#include <string>
#include "whisper.h"

int main() {
    std::cout << "=== CUDA支持检测测试 ===" << std::endl;

    // 检查编译时CUDA支持
#ifdef USE_CUDA
    std::cout << "编译时CUDA支持已启用" << std::endl;
#else
    std::cout << "编译时CUDA支持未启用" << std::endl;
#endif

    // 尝试初始化whisper模型
    std::cout << "尝试加载模型..." << std::endl;
    
    // 设置上下文参数，明确启用GPU
    struct whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = true;
    
    std::cout << "设置参数: use_gpu=" << (cparams.use_gpu ? "true" : "false") 
              << ", gpu_device=" << cparams.gpu_device << std::endl;
    
    // 尝试加载模型文件
    const char* model_paths[] = {
        "models/ggml-tiny.bin",
        "models/ggml-base.bin",
        "models/ggml-small.bin",
        "models/ggml-medium.bin",
        "../models/ggml-tiny.bin",
        "../models/ggml-base.bin",
        "../models/ggml-small.bin",
        "../models/ggml-medium.bin"
    };
    
    struct whisper_context* ctx = nullptr;
    const char* loaded_model = nullptr;
    
    for (const auto& path : model_paths) {
        std::cout << "尝试加载: " << path << std::endl;
        ctx = whisper_init_from_file_with_params(path, cparams);
        if (ctx != nullptr) {
            loaded_model = path;
            break;
        }
    }
    
    if (ctx == nullptr) {
        std::cerr << "错误: 无法加载任何模型文件" << std::endl;
        return 1;
    }
    
    std::cout << "成功加载模型: " << loaded_model << std::endl;
    
    // 运行简单的推理测试
    std::cout << "运行推理测试..." << std::endl;
    
    // 创建一个简单的音频数据（静音）
    const int n_samples = 16000; // 1秒
    std::vector<float> samples(n_samples, 0.0f);
    
    // 设置推理参数
    struct whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_realtime = false;
    wparams.print_progress = false;
    wparams.use_gpu = true; // 明确要求使用GPU
    
    std::cout << "运行GPU推理..." << std::endl;
    
    if (whisper_full(ctx, wparams, samples.data(), samples.size()) != 0) {
        std::cerr << "推理失败" << std::endl;
        whisper_free(ctx);
        return 1;
    }
    
    std::cout << "推理成功完成！" << std::endl;
    
    // 打印推理结果
    const int n_segments = whisper_full_n_segments(ctx);
    std::cout << "识别到 " << n_segments << " 个分段" << std::endl;
    
    for (int i = 0; i < n_segments; ++i) {
        const char* text = whisper_full_get_segment_text(ctx, i);
        std::cout << "分段 " << i << ": " << text << std::endl;
    }
    
    // 清理
    whisper_free(ctx);
    
    std::cout << "测试完成" << std::endl;
    return 0;
} 