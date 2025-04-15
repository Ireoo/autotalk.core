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

#include "../include/audio_capture.h"
#include "../include/system_monitor.h"
#include "../whisper.cpp/include/whisper.h"

// Constants
constexpr int SAMPLE_RATE = 16000;
constexpr int FRAME_SIZE = 512;
constexpr int MAX_BUFFER_SIZE = SAMPLE_RATE * 30;   // 30 seconds of audio
constexpr int AUDIO_CONTEXT_SIZE = SAMPLE_RATE * 1; // 3 seconds context
constexpr int MIN_AUDIO_SAMPLES = SAMPLE_RATE;      // 至少1秒的音频数据

// static const std::regex sentence_end_regex(R"([.!?。！？~])");

// Global variables
std::atomic<bool> running(true);
std::deque<float> audioBuffer;
std::mutex bufferMutex;
whisper_context *ctx = nullptr;
SystemMonitor *systemMonitor = nullptr;

// 使用标准队列替代 boost 无锁队列
const size_t AUDIO_QUEUE_SIZE = 1024; // 队列大小
std::queue<std::vector<float>> audioQueue;
std::mutex audioQueueMutex;

// 音频处理相关的全局变量
std::vector<float> audio_chunk;
std::string confirmInfo;
const int MAX_AUDIO_LENGTH = 10 * SAMPLE_RATE; // 最大音频长度（10秒）
// 识别语音相同内容次数
const int MAX_REPEAT_COUNT = 5;
int REPEAT_COUNT = 0;
std::string REPEAT_TEXT;

// bool is_append = false;
// std::vector<float> repeat_audio;

// Signal handler for Ctrl+C
void signalHandler(int signal)
{
    if (signal == SIGINT)
    {
        running = false;
        std::cout << "\n停止录音并退出..." << std::endl;
    }
}

// Audio data processing callback
void processAudio(const std::vector<float> &buffer)
{
    // 使用互斥锁保护队列操作
    std::lock_guard<std::mutex> lock(audioQueueMutex);
    if (audioQueue.size() < AUDIO_QUEUE_SIZE)
    {
        audioQueue.push(buffer);
    }
}

// Helper function: Convert UTF-8 string to display encoding
std::string convertToLocalEncoding(const char *utf8Text)
{
#ifdef _WIN32
    // 在Windows上使用UTF-8到本地编码的转换
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8Text, -1, nullptr, 0);
    std::vector<wchar_t> wstr(len);
    MultiByteToWideChar(CP_UTF8, 0, utf8Text, -1, wstr.data(), len);

    len = WideCharToMultiByte(CP_ACP, 0, wstr.data(), -1, nullptr, 0, nullptr, nullptr);
    std::vector<char> str(len);
    WideCharToMultiByte(CP_ACP, 0, wstr.data(), -1, str.data(), len, nullptr, nullptr);

    return std::string(str.data());
#else
    // 在Linux上直接返回UTF-8
    return std::string(utf8Text);
#endif
}

void ClearConsoleBlock(HANDLE hConsole, int startRow, int lineCount, int width)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    DWORD written;
    for (int i = 0; i < lineCount; ++i)
    {
        COORD coord = {0, startRow + i};
        FillConsoleOutputCharacter(hConsole, ' ', width, coord, &written);
        // 可选：填充当前行的属性（颜色等）
        FillConsoleOutputAttribute(hConsole, csbi.wAttributes, width, coord, &written);
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
                wparams.audio_ctx = 128; // 保留的音频上下文长度，根据实际使用情况微调

                // 输出与 token 限制
                wparams.max_len = 0;      // 0 表示不限制输出长度（或采用模型默认值）
                wparams.max_tokens = 256; // 可根据语音内容复杂度适当增加

                // Token 时间戳记录
                wparams.token_timestamps = false;

                // 解码温度及相关阈值设置
                wparams.thold_pt = 0.06f;       // token概率的阈值，可确保低概率输出被抑制
                wparams.temperature = 0.0f;     // 温度设置为0，保证贪心解码的确定性
                wparams.temperature_inc = 0.4f; // 不进行温度增量调整
                wparams.entropy_thold = 2.4f;   // 熵阈值，过高可能导致更多噪声输出，过低可能过于保守
                wparams.logprob_thold = -1.0f;  // 对数概率阈值，控制 token 输出的可靠性
                wparams.no_speech_thold = 0.6f; // 无语音判定阈值，用于过滤纯背景噪声

                // 上下文保持：适用于连续语音识别场景
                wparams.no_context = true;

                // 获取当前时间戳
                auto now = std::chrono::system_clock::now();
                auto now_time = std::chrono::system_clock::to_time_t(now);
                std::stringstream ss;
                ss << std::put_time(std::localtime(&now_time), "%Y-%m-%d-%H-%M-%S");
                auto timestamp = ss.str();

                // 复制音频数据以避免异步访问问题
                // std::vector<float> audio_copy;
                // {
                //     std::lock_guard<std::mutex> lock(bufferMutex);
                //     audio_copy = audio_chunk;
                // }

                std::vector<float>::const_iterator audio_end = audio_chunk.end();

                if (whisper_full(ctx, wparams, audio_chunk.data(), audio_chunk.size()) == 0)
                {
                    const int n_segments = whisper_full_n_segments(ctx);
                    std::string recognized_text;
                    for (int i = 0; i < n_segments; ++i)
                    {
                        const char *text = whisper_full_get_segment_text(ctx, i);
                        if (text[0] != '\0')
                        {
                            recognized_text += text;
                        }
                    }

                    if (std::regex_search(recognized_text, std::regex("^(謝謝大家|未经许可,不得翻唱或使用|字幕志愿者|谢谢大家|优优独播剧场——YoYo Television Series Exclusive|请不吝点赞 订阅 转发 打赏支持明镜与点点栏目|by bwd6|謝謝觀看|謝謝觀看|謝謝收看|\\()")))
                    {

                        if (running)
                        {
                            std::lock_guard<std::mutex> lock(bufferMutex);
                            audio_chunk.erase(audio_chunk.begin(), audio_end);
                            CONSOLE_SCREEN_BUFFER_INFO csbi;
                            GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
                            int consoleWidth = csbi.dwSize.X;
                            std::cout << "\r" << std::string(consoleWidth, ' ') << "\r[" << timestamp << "]: ..." << std::flush;
                        }
                    }
                    else if (running)
                    {
                        // 获取控制台句柄及宽度
                        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
                        CONSOLE_SCREEN_BUFFER_INFO csbi;
                        GetConsoleScreenBufferInfo(hConsole, &csbi);
                        int consoleWidth = csbi.dwSize.X;

                        // 将 recognized_text 分割为多行
                        std::istringstream iss("[" + timestamp + "]: " + recognized_text);
                        std::vector<std::string> lines;
                        std::string line;
                        while (std::getline(iss, line))
                        {
                            lines.push_back(line);
                        }

                        // 假设 recognized_text 原来打印的位置从当前行开始，
                        // 保存当前光标行号作为开始行（这里简单示例，实际需要精确控制光标）
                        int startRow = csbi.dwCursorPosition.Y;

                        // 清除 recognized_text 占据的所有行
                        ClearConsoleBlock(hConsole, startRow, lines.size(), consoleWidth);

                        // 移动光标到开始行，并重新打印新的 recognized_text
                        COORD newPos = {0, (SHORT)startRow};
                        SetConsoleCursorPosition(hConsole, newPos);
                        std::cout << "[" << timestamp << "]: " << recognized_text << std::flush;

                        if (std::regex_search(recognized_text, std::regex("[\\.!?。！？~]$")))
                        {
                            std::lock_guard<std::mutex> lock(bufferMutex);
                            audio_chunk.erase(audio_chunk.begin(), audio_end);
                            std::cout << std::endl;
                        }
                    }

                    size_t keep_size = SAMPLE_RATE * 10;
                    if (audio_chunk.size() > keep_size)
                    {
                        std::lock_guard<std::mutex> lock(bufferMutex);
                        audio_chunk.erase(audio_chunk.begin(), audio_chunk.end() - keep_size);
                    }
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "语音识别处理错误: " << e.what() << std::endl;
            }
            catch (...)
            {
                std::cerr << "语音识别处理发生未知错误" << std::endl;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// 音频处理线程函数
void processAudioStream()
{
    while (running)
    {
        std::vector<float> currentAudio;
        bool hasData = false;

        {
            std::lock_guard<std::mutex> lock(audioQueueMutex);
            if (!audioQueue.empty())
            {
                currentAudio = audioQueue.front();
                audioQueue.pop();
                hasData = true;
            }
        }

        if (hasData)
        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            audio_chunk.insert(audio_chunk.end(), currentAudio.begin(), currentAudio.end());
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

int main(int argc, char **argv)
{
    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // 解析命令行参数
    int selectedMic = 0; // 初始值设为-1，表示未指定
    std::string modelPath = "models/ggml-medium-zh.bin";
    bool listDevices = false;

    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg == "--mic" && i + 1 < argc)
        {
            selectedMic = std::stoi(argv[++i]);
        }
        else if (arg == "--model" && i + 1 < argc)
        {
            modelPath = argv[++i];
        }
        else if (arg == "--list")
        {
            listDevices = true;
        }
    }

// 设置中文控制台输出
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    // 初始化音频捕获
    AudioCapture audioCapture;
    if (!audioCapture.initialize())
    {
        std::cerr << "无法初始化音频捕获" << std::endl;
        return 1;
    }

    // 启用环回捕获
    audioCapture.setLoopbackCapture(true);

    // 获取并显示可用的输入设备
    auto devices = audioCapture.getInputDevices();
    std::cout << "\n可用的输入设备：" << std::endl;
    for (const auto &device : devices)
    {
        std::cout << device.first << ": " << device.second << std::endl;
    }

    // 如果指定了 --list 参数，显示设备列表后退出
    if (listDevices)
    {
        return 0;
    }

    // 如果没有指定麦克风，使用列表中的第一个设备
    if (selectedMic == -1)
    {
        if (!devices.empty())
        {
            selectedMic = devices[0].first;
            std::cout << "\n使用默认输入设备：" << selectedMic << " (" << devices[0].second << ")" << std::endl;
        }
        else
        {
            std::cerr << "未找到可用的输入设备" << std::endl;
            return 1;
        }
    }
    else
    {
        std::cout << "\n使用指定的输入设备：" << selectedMic << std::endl;
    }

    std::cout << "正在初始化语音识别系统..." << std::endl;

    // 初始化 whisper 模型
    ctx = whisper_init_from_file(modelPath.c_str());
    if (!ctx)
    {
        std::cerr << "无法加载模型，请确保模型文件 " << modelPath << " 存在" << std::endl;
        return 1;
    }

    // 初始化系统监控
    systemMonitor = new SystemMonitor();
    systemMonitor->start();

    if (!audioCapture.setInputDevice(selectedMic))
    {
        std::cerr << "无法设置输入设备" << std::endl;
        whisper_free(ctx);
        delete systemMonitor;
        return 1;
    }

    // 启动音频处理线程
    std::thread processThread(processAudioStream);
    std::thread recognitionThread(processSpeechRecognition);

    // 启动音频捕获
    if (!audioCapture.start([](const std::vector<float> &buffer)
                            { processAudio(buffer); }))
    {
        std::cerr << "无法启动音频捕获" << std::endl;
        running = false;
        processThread.join();
        recognitionThread.join();
        whisper_free(ctx);
        delete systemMonitor;
        return 1;
    }

    std::cout << "\n系统已启动，正在进行实时语音识别..." << std::endl;
    std::cout << "按 Ctrl+C 停止程序" << std::endl;

    // 等待所有线程结束
    processThread.join();
    recognitionThread.join();

    // 清理资源
    audioCapture.stop();
    whisper_free(ctx);
    delete systemMonitor;

    std::cout << "程序已停止" << std::endl;
    return 0;
}