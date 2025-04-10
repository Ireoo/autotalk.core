#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <deque>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <signal.h>

#include "audio_recorder.h"
#include "whisper.h"

// 设置常量
constexpr int SAMPLE_RATE = 16000;
constexpr int FRAME_SIZE = 512;
constexpr int MAX_BUFFER_SIZE = SAMPLE_RATE * 30; // 最大缓冲区大小（30秒的音频）
constexpr int AUDIO_CONTEXT_SIZE = SAMPLE_RATE * 3; // 3秒的音频上下文

// 全局变量
std::atomic<bool> g_is_running(true);
std::mutex g_audio_mutex;
std::condition_variable g_audio_cv;
std::deque<float> g_audio_buffer;
std::atomic<bool> g_audio_ready(false);

// 信号处理函数，用于优雅退出
void signalHandler(int signal) {
    g_is_running = false;
}

// 将音频数据添加到全局缓冲区
void addAudioData(const std::vector<float>& data) {
    std::lock_guard<std::mutex> lock(g_audio_mutex);
    
    // 添加新的音频数据
    g_audio_buffer.insert(g_audio_buffer.end(), data.begin(), data.end());
    
    // 如果缓冲区过大，移除最旧的数据
    if (g_audio_buffer.size() > MAX_BUFFER_SIZE) {
        size_t to_remove = g_audio_buffer.size() - MAX_BUFFER_SIZE;
        g_audio_buffer.erase(g_audio_buffer.begin(), g_audio_buffer.begin() + to_remove);
    }
    
    // 如果缓冲区大小足够，标记为可以进行处理
    if (g_audio_buffer.size() >= SAMPLE_RATE * 1) { // 至少1秒的音频
        g_audio_ready = true;
        g_audio_cv.notify_one();
    }
}

// 处理音频数据并进行语音识别的线程函数
void processAudioThread(struct whisper_context* ctx) {
    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    
    wparams.print_realtime = true;
    wparams.print_progress = false;
    wparams.print_timestamps = true;
    wparams.translate = false;
    wparams.language = "zh"; // 设置为中文，可根据需要修改
    wparams.n_threads = std::thread::hardware_concurrency();
    wparams.audio_ctx = AUDIO_CONTEXT_SIZE;
    
    // 设置GPU加速
    wparams.use_gpu = true;
    
    std::vector<float> audio_data;
    std::string last_text;
    
    while (g_is_running) {
        {
            std::unique_lock<std::mutex> lock(g_audio_mutex);
            g_audio_cv.wait(lock, [] { return g_audio_ready || !g_is_running; });
            
            if (!g_is_running) {
                break;
            }
            
            // 复制当前缓冲区中的所有音频数据
            audio_data.assign(g_audio_buffer.begin(), g_audio_buffer.end());
            
            // 清空缓冲区或仅保留最后一部分作为上下文
            if (g_audio_buffer.size() > AUDIO_CONTEXT_SIZE) {
                size_t to_keep = AUDIO_CONTEXT_SIZE;
                g_audio_buffer.erase(g_audio_buffer.begin(), g_audio_buffer.end() - to_keep);
            }
            
            g_audio_ready = false;
        }
        
        // 运行Whisper进行语音识别
        if (whisper_full(ctx, wparams, audio_data.data(), audio_data.size()) != 0) {
            std::cerr << "语音识别失败" << std::endl;
            continue;
        }
        
        // 获取识别结果
        int n_segments = whisper_full_n_segments(ctx);
        if (n_segments > 0) {
            std::string text;
            for (int i = 0; i < n_segments; ++i) {
                const char* segment_text = whisper_full_get_segment_text(ctx, i);
                text += segment_text;
            }
            
            // 只有当文本发生变化时才输出
            if (text != last_text && !text.empty()) {
                std::cout << "识别结果: " << text << std::endl;
                last_text = text;
            }
        }
    }
}

int main(int argc, char** argv) {
    // 设置信号处理器
    signal(SIGINT, signalHandler);
    
    // 检查命令行参数
    if (argc < 2) {
        std::cerr << "用法: " << argv[0] << " <模型路径>" << std::endl;
        return 1;
    }
    
    std::string model_path = argv[1];
    
    // 初始化whisper.cpp
    std::cout << "加载Whisper模型: " << model_path << std::endl;
    
    // 使用新的API，避免过时警告
    whisper_context_params cparams = whisper_context_default_params();
    
    // 强制启用GPU，并设置合适的参数
    cparams.use_gpu = true;
    
    // 检查系统中是否有可用的CUDA设备
    int deviceCount = 0;
    if (cudaGetDeviceCount(&deviceCount) != cudaSuccess || deviceCount == 0) {
        std::cerr << "警告: 未检测到CUDA设备，将回退到CPU模式" << std::endl;
        cparams.use_gpu = false;
    } else {
        std::cout << "检测到 " << deviceCount << " 个CUDA设备，使用GPU加速" << std::endl;
    }
    
    // 优先尝试使用GPU加载模型
    struct whisper_context* ctx = whisper_init_from_file_with_params(model_path.c_str(), cparams);
    
    if (ctx == nullptr) {
        std::cerr << "无法使用GPU加载模型，尝试使用CPU..." << std::endl;
        // 禁用GPU，使用CPU
        cparams.use_gpu = false;
        ctx = whisper_init_from_file_with_params(model_path.c_str(), cparams);
        
        if (ctx == nullptr) {
            std::cerr << "无法加载模型: " << model_path << std::endl;
            return 1;
        }
        std::cout << "使用CPU运行模型" << std::endl;
    } else {
        std::cout << "使用GPU运行模型" << std::endl;
    }
    
    // 初始化音频录制器
    AudioRecorder recorder;
    if (!recorder.init(SAMPLE_RATE, FRAME_SIZE)) {
        std::cerr << "初始化音频录制器失败" << std::endl;
        whisper_free(ctx);
        return 1;
    }
    
    // 创建语音识别线程
    std::thread process_thread(processAudioThread, ctx);
    
    // 开始录音
    std::cout << "开始录音，按Ctrl+C退出..." << std::endl;
    if (!recorder.start(addAudioData)) {
        std::cerr << "开始录音失败" << std::endl;
        g_is_running = false;
        g_audio_cv.notify_all();
        process_thread.join();
        whisper_free(ctx);
        return 1;
    }
    
    // 等待结束信号
    while (g_is_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 停止录音
    std::cout << "正在停止..." << std::endl;
    recorder.stop();
    
    // 等待处理线程结束
    g_audio_cv.notify_all();
    process_thread.join();
    
    // 清理资源
    whisper_free(ctx);
    
    std::cout << "程序已退出" << std::endl;
    
    return 0;
} 