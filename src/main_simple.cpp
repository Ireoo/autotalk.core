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
#include <fcntl.h>
#include <fstream>  // 添加文件输出支持

// Windows需要特殊处理控制台编码
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#endif

#include "audio_recorder.h"
#include "whisper.h"

// Constants
constexpr int SAMPLE_RATE = 16000;
constexpr int FRAME_SIZE = 512;
constexpr int MAX_BUFFER_SIZE = SAMPLE_RATE * 30; // 30 seconds of audio
constexpr int AUDIO_CONTEXT_SIZE = 1500; // Reduced audio context size (max allowed is 1500)

// Global variables
std::atomic<bool> g_is_running(true);
std::mutex g_audio_mutex;
std::condition_variable g_audio_cv;
std::deque<float> g_audio_buffer;
std::atomic<bool> g_audio_ready(false);
std::ofstream g_log_file;  // 日志文件

// 打开日志文件
void openLogFile(const std::string& filename = "autotalk_debug.log") {
    g_log_file.open(filename);
    if (!g_log_file.is_open()) {
        std::cerr << "无法打开日志文件: " << filename << std::endl;
    } else {
        g_log_file << "=== AutoTalk调试日志 ===" << std::endl;
        g_log_file << "时间: " << std::chrono::system_clock::now().time_since_epoch().count() << std::endl;
    }
}

// 写入日志
void logMessage(const std::string& message) {
    if (g_log_file.is_open()) {
        g_log_file << message << std::endl;
        g_log_file.flush();
    }
}

// Set console to UTF-8 mode on Windows
void setConsoleUTF8() {
#ifdef _WIN32
    try {
        // Set console code page to UTF-8
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        logMessage("设置控制台代码页为UTF-8成功");
        
        // 我们不再强制使用宽字符输出，这可能导致问题
        /*
        try {
            _setmode(_fileno(stdout), _O_U8TEXT);
            _setmode(_fileno(stdin), _O_U8TEXT);
            _setmode(_fileno(stderr), _O_U8TEXT);
            std::wcout << L"UTF-8编码设置成功" << std::endl;
        } catch (...) {
            std::cerr << "设置UTF-8编码模式出错，将使用默认编码" << std::endl;
            logMessage("设置UTF-8编码模式出错，将使用默认编码");
        }
        */
    } catch (...) {
        std::cerr << "设置控制台编码出错" << std::endl;
        logMessage("设置控制台编码出错");
    }
#endif
}

// Signal handler for graceful exit
void signalHandler(int signal) {
    g_is_running = false;
    logMessage("收到信号，程序准备退出");
}

// Add audio data to the global buffer
void addAudioData(const std::vector<float>& data) {
    std::lock_guard<std::mutex> lock(g_audio_mutex);
    
    // Add new audio data
    g_audio_buffer.insert(g_audio_buffer.end(), data.begin(), data.end());
    
    // Remove oldest data if buffer is too large
    if (g_audio_buffer.size() > MAX_BUFFER_SIZE) {
        size_t to_remove = g_audio_buffer.size() - MAX_BUFFER_SIZE;
        g_audio_buffer.erase(g_audio_buffer.begin(), g_audio_buffer.begin() + to_remove);
    }
    
    // Mark as ready for processing if we have enough data
    if (g_audio_buffer.size() >= SAMPLE_RATE * 1) { // At least 1 second of audio
        g_audio_ready = true;
        g_audio_cv.notify_one();
    }
}

// Process audio data and perform speech recognition
void processAudioThread(struct whisper_context* ctx) {
    logMessage("开始音频处理线程");
    
    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    
    wparams.print_realtime = true;
    wparams.print_progress = false;
    wparams.print_timestamps = true;
    wparams.translate = false;
    wparams.language = "zh"; // Set to Chinese, can be modified as needed
    wparams.n_threads = std::thread::hardware_concurrency();
    wparams.audio_ctx = AUDIO_CONTEXT_SIZE;
    
    // 启用GPU加速
#ifdef USE_CUDA
    wparams.use_gpu = true;
    logMessage("启用GPU加速");
#else
    wparams.use_gpu = false;
    logMessage("使用CPU计算");
#endif
    
    logMessage("设置Whisper参数: 线程数=" + std::to_string(wparams.n_threads) + 
               ", 语言=" + std::string(wparams.language) + 
               ", 使用GPU=" + (wparams.use_gpu ? "是" : "否"));
    
    std::vector<float> audio_data;
    std::string last_text;
    
    while (g_is_running) {
        {
            std::unique_lock<std::mutex> lock(g_audio_mutex);
            g_audio_cv.wait(lock, [] { return g_audio_ready || !g_is_running; });
            
            if (!g_is_running) {
                break;
            }
            
            // Copy all audio data in the current buffer
            audio_data.assign(g_audio_buffer.begin(), g_audio_buffer.end());
            
            // Clear buffer or only keep the last part as context
            if (g_audio_buffer.size() > AUDIO_CONTEXT_SIZE) {
                size_t to_keep = AUDIO_CONTEXT_SIZE;
                g_audio_buffer.erase(g_audio_buffer.begin(), g_audio_buffer.end() - to_keep);
            }
            
            g_audio_ready = false;
        }
        
        // Run Whisper for speech recognition
        logMessage("处理音频数据，大小=" + std::to_string(audio_data.size()));
        
        if (whisper_full(ctx, wparams, audio_data.data(), audio_data.size()) != 0) {
            std::cerr << "语音识别失败" << std::endl;
            logMessage("语音识别失败");
            continue;
        }
        
        // Get recognition results
        int n_segments = whisper_full_n_segments(ctx);
        logMessage("识别获得 " + std::to_string(n_segments) + " 个分段");
        
        if (n_segments > 0) {
            std::string text;
            for (int i = 0; i < n_segments; ++i) {
                const char* segment_text = whisper_full_get_segment_text(ctx, i);
                text += segment_text;
            }
            
            // Only output when text changes
            if (text != last_text && !text.empty()) {
                std::cout << "识别结果: " << text << std::endl;
                logMessage("识别结果: " + text);
                last_text = text;
            }
        }
    }
    
    logMessage("音频处理线程结束");
}

int main(int argc, char** argv) {
    // 打开日志文件
    openLogFile();
    logMessage("程序启动");
    
    // Set console to UTF-8 mode
    setConsoleUTF8();
    
    // Set signal handler
    signal(SIGINT, signalHandler);
    
    // 添加参数检查和显示版权信息
    std::cout << "\n=== AutoTalk 实时语音转文字工具 ===\n" << std::endl;
    
    logMessage("命令行参数: " + std::to_string(argc));
    for (int i = 0; i < argc; i++) {
        logMessage("  arg[" + std::to_string(i) + "] = " + std::string(argv[i]));
    }
    
    // Check command line arguments
    if (argc < 2) {
        std::cerr << "用法: " << argv[0] << " <模型路径>" << std::endl;
        logMessage("缺少模型路径参数");
        return 1;
    }
    
    std::string model_path = argv[1];
    logMessage("使用模型: " + model_path);
    
    // Initialize whisper.cpp
    std::cout << "加载Whisper模型: " << model_path << std::endl;
    
    logMessage("加载Whisper模型...");
    
    // Use new API to avoid deprecation warning
    whisper_context_params cparams = whisper_context_default_params();
    
    // 设置GPU参数
#ifdef USE_CUDA
    cparams.use_gpu = true;
    std::cout << "启用GPU加速" << std::endl;
    logMessage("启用GPU加速");
#else
    cparams.use_gpu = false;
    std::cout << "使用CPU计算" << std::endl;
    logMessage("使用CPU计算");
#endif
    
    std::cout << "使用参数: n_threads=" << std::thread::hardware_concurrency() 
              << ", 使用GPU=" << (cparams.use_gpu ? "是" : "否") << std::endl;
    
    try {
        std::cout << "正在初始化模型..." << std::endl;
        logMessage("初始化模型中...");
        
        struct whisper_context* ctx = whisper_init_from_file_with_params(model_path.c_str(), cparams);
        
        std::cout << "模型初始化完成，检查结果..." << std::endl;
        logMessage("模型初始化完成");
        
        if (ctx == nullptr) {
            std::cerr << "无法加载模型: " << model_path << std::endl;
            logMessage("模型加载失败");
            return 1;
        }
        
        logMessage("模型加载成功");
        
        std::cout << "模型加载成功，初始化音频录制器..." << std::endl;
        
        // Initialize audio recorder
        AudioRecorder recorder;
        logMessage("初始化音频录制器...");
        
        if (!recorder.init(SAMPLE_RATE, FRAME_SIZE)) {
            std::cerr << "初始化音频录制器失败" << std::endl;
            logMessage("音频录制器初始化失败");
            whisper_free(ctx);
            return 1;
        }
        
        logMessage("音频录制器初始化成功");
        
        std::cout << "音频录制器初始化成功，创建处理线程..." << std::endl;
        
        // Create speech recognition thread
        logMessage("创建识别线程...");
        std::thread process_thread(processAudioThread, ctx);
        
        // Start recording
        std::cout << "开始录音，按Ctrl+C退出..." << std::endl;
        
        logMessage("开始录音...");
        if (!recorder.start(addAudioData)) {
            std::cerr << "开始录音失败" << std::endl;
            logMessage("开始录音失败");
            g_is_running = false;
            g_audio_cv.notify_all();
            process_thread.join();
            whisper_free(ctx);
            return 1;
        }
        
        logMessage("录音开始成功");
        
        // Wait for exit signal
        while (g_is_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Stop recording
        std::cout << "正在停止..." << std::endl;
        
        logMessage("停止中...");
        recorder.stop();
        
        // Wait for processing thread to end
        g_audio_cv.notify_all();
        process_thread.join();
        
        // Clean up resources
        whisper_free(ctx);
        
        std::cout << "程序已退出" << std::endl;
        
        logMessage("程序正常退出");
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "发生异常: " << e.what() << std::endl;
        logMessage("发生异常: " + std::string(e.what()));
        return 1;
    }
    catch (...) {
        std::cerr << "发生未知异常！" << std::endl;
        logMessage("发生未知异常");
        return 1;
    }
} 