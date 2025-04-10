#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include "whisper.h"

void log_model_info(const std::string& model_path) {
    std::cout << "尝试加载模型: " << model_path << std::endl;
    
    // 检查文件是否存在
    std::ifstream file(model_path, std::ios::binary);
    if (!file) {
        std::cerr << "错误: 文件不存在或无法打开: " << model_path << std::endl;
        return;
    }
    
    // 获取文件大小
    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    file.close();
    
    std::cout << "文件存在，大小: " << size << " 字节" << std::endl;
}

int main() {
    std::cout << "模型测试程序启动" << std::endl;

    // 尝试加载模型
    const std::string model_paths[] = {
        "models/ggml-tiny.bin",
        "models/ggml-tiny.en.bin",
        "models/ggml-base.bin",
        "models/ggml-base.en.bin",
        "models/ggml-small.bin",
        "models/ggml-small.en.bin",
        "models/ggml-medium.bin",
        "models/ggml-medium.en.bin",
        "models/ggml-large.bin",
        "../models/ggml-tiny.bin",
        "../models/ggml-tiny.en.bin",
        "../models/ggml-base.bin",
        "../models/ggml-base.en.bin",
        "../models/ggml-small.bin",
        "../models/ggml-small.en.bin",
        "../models/ggml-medium.bin",
        "../models/ggml-medium.en.bin",
        "../models/ggml-large.bin"
    };

    for (const auto& model_path : model_paths) {
        log_model_info(model_path);
        
        try {
            struct whisper_context * ctx = whisper_init_from_file(model_path.c_str());
            
            if (ctx != nullptr) {
                std::cout << "成功加载模型: " << model_path << std::endl;
                std::cout << "模型类型: " << whisper_model_type(ctx) << std::endl;
                whisper_free(ctx);
                // 成功加载一个模型后退出
                return 0;
            } else {
                std::cerr << "无法加载模型: " << model_path << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "加载模型时发生异常: " << e.what() << std::endl;
        }
    }

    std::cerr << "错误: 无法加载任何模型。" << std::endl;
    return 1;
} 