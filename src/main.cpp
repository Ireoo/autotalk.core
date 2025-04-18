#include <iostream>
#include <vector>
#include <deque>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <signal.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <locale>
#include <codecvt>
#include <queue>
#include <limits>
#include <iomanip>
#include <regex>
#include <sstream>
#include <future>
#include <condition_variable>

#include "../include/audio_server.h"
#include "../include/system_monitor.h"
#include "../whisper.cpp/include/whisper.h"

// Constants
constexpr int SAMPLE_RATE = 16000;
constexpr int SAMPLE_RATE_KONG = 0;
constexpr int FRAME_SIZE = 512;
constexpr int MAX_BUFFER_SIZE = SAMPLE_RATE * 30;   // 30 seconds of audio
constexpr int AUDIO_CONTEXT_SIZE = SAMPLE_RATE * 1; // 3 seconds context
constexpr int MIN_AUDIO_SAMPLES = SAMPLE_RATE;      // 至少1秒的音频数据

// Global variables
std::atomic<bool> running(true);
std::deque<float> audioBuffer;
std::mutex bufferMutex;
whisper_context *ctx = nullptr;
SystemMonitor *systemMonitor = nullptr;
AudioServer *audioServer = nullptr;

// 检查是否启用GPU
#ifdef USE_GPU
bool useGPU = true; // 如果编译时定义了USE_GPU，则默认启用
#else
bool useGPU = false; // 否则默认禁用
#endif

// 音频处理相关的全局变量
std::vector<float> audio_chunk;
std::string confirmInfo;
const int MAX_AUDIO_LENGTH = 10 * SAMPLE_RATE; // 最大音频长度（10秒）
// 识别语音相同内容次数
const int MAX_REPEAT_COUNT = 3;
int REPEAT_COUNT = 0;
std::string REPEAT_TEXT;

static const std::regex pattern(R"(。+$)", std::regex::optimize);
static const std::regex pattern_dou(R"(^[,，]+)", std::regex::optimize);
std::string last_recognized_text;

// Signal handler for Ctrl+C
void signalHandler(int signal)
{
    if (signal == SIGINT)
    {
        running = false;
        std::cout << "\n停止服务并退出..." << std::endl;
    }
}

// Audio data processing callback
void processAudio(const std::vector<float> &buffer)
{
    std::lock_guard<std::mutex> lock(bufferMutex);

    // 将新的音频数据添加到缓冲区
    audioBuffer.insert(audioBuffer.end(), buffer.begin(), buffer.end());

    // 限制缓冲区大小
    if (audioBuffer.size() > MAX_BUFFER_SIZE)
    {
        audioBuffer.erase(audioBuffer.begin(), audioBuffer.begin() + (audioBuffer.size() - MAX_BUFFER_SIZE));
    }
}

// 语音识别处理线程函数
void processSpeechRecognition()
{
    while (running)
    {
        if (audio_chunk.size() >= SAMPLE_RATE)
        {
            try
            {
                whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
                // 输出控制：关闭实时及进度打印，开启时间戳显示
                wparams.print_realtime = false;
                wparams.print_progress = false;
                wparams.print_timestamps = false;

                // 语言与翻译设置
                wparams.language = "zh";   // 强制使用中文识别
                wparams.translate = false; // 不进行翻译，只转录原语言

                // 线程设置：采用硬件并发数，加速计算
                wparams.n_threads = std::thread::hardware_concurrency();

                // 音频截取设置
                wparams.offset_ms = 0;   // 从音频起始开始处理
                wparams.duration_ms = 0; // 0 表示处理整个输入音频
                wparams.audio_ctx = 0;   // 保留的音频上下文长度，根据实际使用情况微调

                // 输出与 token 限制
                wparams.max_len = 0;      // 0 表示不限制输出长度（或采用模型默认值）
                wparams.max_tokens = 128; // 可根据语音内容复杂度适当增加

                // Token 时间戳记录
                wparams.token_timestamps = true;
                wparams.thold_pt = 0.01f; // 降低时间戳阈值以获取更精确的结果

                // 解码温度及相关阈值设置
                wparams.temperature = 0.0f;     // 温度设置为0，保证贪心解码的确定性
                wparams.temperature_inc = 0.0f; // 不进行温度增量调整
                wparams.entropy_thold = 2.6f;   // 熵阈值，过高可能导致更多噪声输出，过低可能过于保守
                wparams.logprob_thold = -1.0f;  // 对数概率阈值，控制 token 输出的可靠性
                wparams.no_speech_thold = 0.6f; // 无语音判定阈值，用于过滤纯背景噪声

                // 上下文保持：适用于连续语音识别场景
                wparams.no_context = true;

                // 获取当前时间戳
                auto now = std::chrono::system_clock::now();
                auto now_time = std::chrono::system_clock::to_time_t(now);
                std::stringstream ss;
                ss << std::put_time(std::localtime(&now_time), "%Y-%m-%d-%H-%M-%S");
                std::string timestamp = ss.str();

                // 复制音频数据以避免异步访问问题
                std::vector<float> audio_copy;
                {
                    std::lock_guard<std::mutex> lock(bufferMutex);
                    audio_copy = audio_chunk;
                }

                if (whisper_full(ctx, wparams, audio_copy.data(), audio_copy.size()) == 0)
                {
                    // 处理每个段落的识别结果
                    int num_segments = whisper_full_n_segments(ctx);
                    std::string recognized_text;
                    std::string full_text;

                    for (int i = 0; i < num_segments; i++)
                    {
                        const char *text = whisper_full_get_segment_text(ctx, i);
                        recognized_text = text;
                        full_text += recognized_text;
                    }

                    std::regex punctuation_regex("[。？！，、：；]");
                    std::sregex_iterator it(full_text.begin(), full_text.end(), punctuation_regex);
                    std::sregex_iterator end;

                    // 计算标点符号数量
                    size_t punctuation_count = std::distance(it, end);

                    // 如果有足够的标点符号或文本足够长，认为这是一个完整的句子
                    bool is_complete_sentence = punctuation_count >= 2 || full_text.length() > 30;

                    // 发送实时识别结果（带L:前缀）
                    if (!full_text.empty())
                    {
                        if (audioServer)
                        {
                            audioServer->sendLiveResult(full_text);
                        }

                        std::cout << "L:" << full_text << std::endl;
                    }

                    // 如果是完整句子，发送完整识别结果（带T:前缀）
                    if (is_complete_sentence && !full_text.empty())
                    {
                        if (full_text != last_recognized_text)
                        {
                            if (audioServer)
                            {
                                audioServer->sendCompleteResult(full_text);
                            }

                            std::cout << "T:" << full_text << std::endl;
                            last_recognized_text = full_text;

                            // 清空音频缓冲区，开始新的识别
                            std::lock_guard<std::mutex> lock(bufferMutex);
                            audio_chunk.clear();
                        }
                    }
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "语音识别异常: " << e.what() << std::endl;
            }
        }

        // 从主缓冲区更新音频块
        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            if (audioBuffer.size() >= MIN_AUDIO_SAMPLES)
            {
                audio_chunk.assign(audioBuffer.begin(), audioBuffer.end());
            }
        }

        // 避免CPU占用过高
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void processAudioStream()
{
    while (running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void monitor()
{
    // 显示系统状态
    systemMonitor->update();

    // 显示当前时间
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm;
#ifdef _WIN32
    localtime_s(&now_tm, &now_time_t);
#else
    now_tm = *localtime(&now_time_t);
#endif
    std::cout << "\n[" << std::put_time(&now_tm, "%H:%M:%S") << "] ";

    // 基本系统信息，添加CPU核心数
    std::cout << "CPU: " << std::fixed << std::setprecision(1)
              << systemMonitor->getCpuUsage() << "% ("
              << systemMonitor->getCPUCores() << "核) | RAM: "
              << std::fixed << std::setprecision(1)
              << systemMonitor->getMemoryUsageMB() << "MB";

    // 获取GPU总数
    int gpuCount = systemMonitor->getGPUCount();
    int activeGPU = systemMonitor->getActiveGPU();

    // 如果有GPU，显示所有GPU信息
    if (gpuCount > 0)
    {
        auto allGPUs = systemMonitor->getAllGPUs();

        for (int i = 0; i < gpuCount; i++)
        {
            const auto &gpu = allGPUs[i];
            std::string activeTag = (i == activeGPU) ? " [Whisper正在使用]" : "";

            std::cout << std::endl
                      << "GPU " << i << ": " << gpu.gpuName << activeTag
                      << " | 使用率: " << std::fixed << std::setprecision(1)
                      << gpu.currentUsage << "%";

            // 显示GPU内存使用情况 - 修复显示顺序
            std::cout << " | GPU内存: " << std::fixed << std::setprecision(1)
                      << gpu.memoryUsageMB << "MB/"
                      << std::fixed << std::setprecision(1)
                      << gpu.memoryTotalMB << "MB ("
                      << std::fixed << std::setprecision(1)
                      << gpu.memoryUsagePercent << "%)";

            // 显示GPU温度、功耗和驱动版本
            std::cout << " | 温度: " << std::fixed << std::setprecision(1)
                      << gpu.temperature << "°C"
                      << " | 功耗: " << std::fixed << std::setprecision(1)
                      << gpu.power << "W"
                      << " | 驱动版本: " << gpu.driverVersion;
        }
    }
    else if (systemMonitor->isGPUAvailable())
    {
        // 兼容单GPU情况的旧代码
        std::string activeTag = useGPU ? " [Whisper正在使用]" : "";
        std::cout << std::endl
                  << "GPU: " << systemMonitor->getGPUName() << activeTag
                  << " | 使用率: " << std::fixed << std::setprecision(1)
                  << systemMonitor->getGPUUsage() << "%";

        // 显示GPU内存使用情况 - 修复显示顺序
        std::cout << " | GPU内存: " << std::fixed << std::setprecision(1)
                  << systemMonitor->getGPUMemoryUsageMB() << "MB/"
                  << std::fixed << std::setprecision(1)
                  << systemMonitor->getGPUMemoryTotalMB() << "MB ("
                  << std::fixed << std::setprecision(1)
                  << systemMonitor->getGPUMemoryPercent() << "%)";

        // 显示GPU温度、功耗和驱动版本
        std::cout << " | 温度: " << std::fixed << std::setprecision(1)
                  << systemMonitor->getGPUTemperature() << "°C"
                  << " | 功耗: " << std::fixed << std::setprecision(1)
                  << systemMonitor->getGPUPower() << "W"
                  << " | 驱动版本: " << systemMonitor->getGPUDriverVersion();
    }

    std::cout << std::flush;
}

int main(int argc, char **argv)
{
// 设置中文控制台输出
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    // 注册信号处理函数
    signal(SIGINT, signalHandler);

    // 输出参数信息
    std::cout << "=== 语音实时识别系统 ===" << std::endl;

    int server_port = 3000;
    bool list_models = false;
    std::string model_path = "models/ggml-base.bin";
    bool test_mode = false;

    // 解析命令行参数
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
        {
            server_port = std::stoi(argv[i + 1]);
            i++;
        }
        else if (strcmp(argv[i], "--model") == 0 && i + 1 < argc)
        {
            model_path = argv[i + 1];
            i++;
        }
        else if (strcmp(argv[i], "--list") == 0)
        {
            list_models = true;
        }
        else if (strcmp(argv[i], "--gpu") == 0)
        {
            useGPU = true;
            std::cout << "启用GPU模式" << std::endl;
        }
        else if (strcmp(argv[i], "--test") == 0)
        {
            test_mode = true;
            std::cout << "启用测试模式，只显示状态" << std::endl;
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            std::cout << "使用方法: " << argv[0] << " [选项]" << std::endl;
            std::cout << "选项:" << std::endl;
            std::cout << "  --port <端口>   指定服务器监听端口 (默认: 3000)" << std::endl;
            std::cout << "  --model <路径>  指定Whisper模型路径 (默认: models/ggml-base.bin)" << std::endl;
            std::cout << "  --list          列出已安装的模型" << std::endl;
            std::cout << "  --gpu           启用GPU支持" << std::endl;
            std::cout << "  --test          测试模式，只显示状态" << std::endl;
            std::cout << "  --help          显示此帮助信息" << std::endl;
            return 0;
        }
    }

    if (test_mode)
    {
        // 测试模式：模拟系统状态显示
        monitor();
    }

    // 列出模型
    if (list_models)
    {
        std::cout << "模型目录: models/" << std::endl;
        try
        {
            if (std::filesystem::exists("models") && std::filesystem::is_directory("models"))
            {
                bool found = false;
                for (const auto &entry : std::filesystem::directory_iterator("models"))
                {
                    if (entry.path().extension() == ".bin")
                    {
                        std::cout << "  " << entry.path().filename().string() << std::endl;
                        found = true;
                    }
                }
                if (!found)
                {
                    std::cout << "  未找到模型文件" << std::endl;
                }
            }
            else
            {
                std::cout << "  模型目录不存在" << std::endl;
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "列出模型时出错: " << e.what() << std::endl;
        }
        return 0;
    }

    // 检查模型文件是否存在
    if (!std::filesystem::exists(model_path))
    {
        std::cerr << "错误: 未找到模型文件 " << model_path << std::endl;
        std::cerr << "请将Whisper模型文件放在models目录下，或使用--model指定模型路径" << std::endl;
        return 1;
    }

    // 初始化系统监控
    systemMonitor = new SystemMonitor();
    if (!systemMonitor->initialize())
    {
        std::cerr << "初始化系统监控失败" << std::endl;
        delete systemMonitor;
        return 1;
    }

    // 初始化Whisper上下文
    std::cout << "正在加载Whisper模型: " << model_path << std::endl;
    whisper_context_params wparams = whisper_context_default_params();
    if (useGPU)
    {
        // 设置GPU设备号，默认使用第一个GPU（索引0）
        wparams.use_gpu = true;
        wparams.gpu_device = 0; // 默认使用第一个GPU
    }
    ctx = whisper_init_from_file_with_params(model_path.c_str(), wparams);
    if (ctx == nullptr)
    {
        std::cerr << "加载Whisper模型失败" << std::endl;
        delete systemMonitor;
        return 1;
    }

    // 如果使用GPU，告知系统监控Whisper正在使用的GPU索引
    if (useGPU)
    {
        // 获取whisper正在使用的GPU设备号
        int whisperGPUIndex = wparams.gpu_device;
        systemMonitor->setActiveGPU(whisperGPUIndex);
        std::cout << "Whisper使用GPU索引: " << whisperGPUIndex << std::endl;
    }

    // 初始化Socket.IO服务器
    audioServer = new AudioServer();
    if (!audioServer->initialize(server_port))
    {
        std::cerr << "初始化Socket.IO服务器失败" << std::endl;
        whisper_free(ctx);
        delete systemMonitor;
        delete audioServer;
        return 1;
    }

    // 启动服务器
    std::cout << "启动Socket.IO服务器，监听端口: " << server_port << std::endl;
    if (!audioServer->start(processAudio))
    {
        std::cerr << "启动Socket.IO服务器失败" << std::endl;
        whisper_free(ctx);
        delete systemMonitor;
        delete audioServer;
        return 1;
    }

    // 启动音频流处理线程
    std::thread processThread(processAudioStream);
    // 启动语音识别线程
    std::thread recognitionThread(processSpeechRecognition);

    std::cout << "服务已启动，按Ctrl+C退出" << std::endl;
    std::cout << "等待客户端连接..." << std::endl;

    // 等待信号处理函数设置running为false
    while (running)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        monitor();
    }

    // 清理资源
    std::cout << "正在关闭服务..." << std::endl;

    processThread.join();
    recognitionThread.join();

    if (audioServer)
    {
        audioServer->stop();
        delete audioServer;
    }

    if (ctx)
    {
        whisper_free(ctx);
    }

    if (systemMonitor)
    {
        delete systemMonitor;
    }

    std::cout << "服务已关闭" << std::endl;
    return 0;
}