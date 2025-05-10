#ifdef _WIN32
#include <windows.h>
#endif

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
#include <algorithm>
#include <map>

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

// 使用标准队列替代 boost 无锁队列
const size_t AUDIO_QUEUE_SIZE = 1024; // 队列大小
std::queue<AudioData> audioQueue;
std::mutex audioQueueMutex;

// 音频处理相关的全局变量
std::mutex userDataMutex;
std::map<std::string, std::vector<float>> audio_chunks;
std::map<std::string, std::vector<float>::iterator> audio_chunk_begins;
std::map<std::string, size_t> audio_chunk_lasts;
std::string confirmInfo;
const int MAX_AUDIO_LENGTH = 20 * SAMPLE_RATE; // 最大音频长度（10秒）
// 识别语音相同内容次数
const int MAX_REPEAT_COUNT = 100;
std::map<std::string, int> REPEAT_COUNTS;
std::map<std::string, std::string> REPEAT_TEXTS;
std::map<std::string, std::string> last_recognized_texts; // 上次识别的文本
std::map<std::string, std::string> last_complete_texts;   // 上次发送的完整文本

static const std::regex pattern(R"(。+$)", std::regex::optimize);
static const std::regex pattern_dian(R"(^\\.)", std::regex::optimize);
static const std::regex pattern_dou(R"(^[,，]+)", std::regex::optimize);
static const std::regex pattern_dd(R"(，)", std::regex::optimize);
static const std::regex pattern_ju(R"(。)", std::regex::optimize);
static const std::regex pattern_gan(R"(！)", std::regex::optimize);
static const std::regex pattern_wen(R"(？)", std::regex::optimize);

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
void processAudio(const std::vector<float> &buffer, const std::string &clientId)
{
    // 使用互斥锁保护队列操作
    std::lock_guard<std::mutex> lock(audioQueueMutex);
    if (audioQueue.size() < AUDIO_QUEUE_SIZE)
    {
        // 将客户端ID与音频数据一起保存
        AudioData data;
        data.buffer = buffer;
        data.clientId = clientId;
        audioQueue.push(data);
    }
}

#ifdef _WIN32
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
#else
// Linux版本的控制台清理函数（简化版）
void ClearConsoleBlock(int startRow, int lineCount, int width)
{
    // 使用ANSI转义序列清除屏幕指定区域
    for (int i = 0; i < lineCount; ++i)
    {
        // 移动光标到指定位置
        printf("\033[%d;1H", startRow + i);
        // 清除整行
        printf("\033[2K");
    }
}
#endif

void clearConsole(std::string txt, bool _lines = true)
{
#ifdef _WIN32
    if (_lines)
    {
        // 获取控制台句柄及宽度
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(hConsole, &csbi);
        int consoleWidth = csbi.dwSize.X;

        // 将 recognized_text 分割为多行
        std::istringstream iss(txt);
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
        std::cout << txt << std::flush;
    }
    else
    {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        int consoleWidth = csbi.dwSize.X;

        std::cout << "\r" << std::string(consoleWidth, ' ') << "\r" << txt << std::flush;
    }
#else
    // Linux版本的控制台清理
    if (_lines)
    {
        // 将文本分割为多行
        std::istringstream iss(txt);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(iss, line))
        {
            lines.push_back(line);
        }

        // 清除屏幕上的文本并重新打印
        printf("\033[s");                       // 保存光标位置
        ClearConsoleBlock(0, lines.size(), 80); // 假定80列宽度
        printf("\033[u");                       // 恢复光标位置
        std::cout << txt << std::flush;
    }
    else
    {
        // 清除当前行并重新打印
        std::cout << "\r\033[K" << txt << std::flush;
    }
#endif
}

// 语音识别处理线程函数
void processSpeechRecognition()
{
    while (running)
    {
        // 获取所有客户端ID的列表
        std::vector<std::string> clientIds;
        {
            std::lock_guard<std::mutex> lock(userDataMutex);
            for (const auto &pair : audio_chunks)
            {
                clientIds.push_back(pair.first);
            }
        }

        // 为每个客户端处理音频
        for (const std::string &clientId : clientIds)
        {
            // 如果是新客户端，初始化相关数据
            {
                std::lock_guard<std::mutex> lock(userDataMutex);
                if (audio_chunk_begins.find(clientId) == audio_chunk_begins.end())
                {
                    audio_chunk_begins[clientId] = audio_chunks[clientId].begin();
                }
                if (audio_chunk_lasts.find(clientId) == audio_chunk_lasts.end())
                {
                    audio_chunk_lasts[clientId] = 0;
                }
                if (REPEAT_COUNTS.find(clientId) == REPEAT_COUNTS.end())
                {
                    REPEAT_COUNTS[clientId] = 0;
                }
            }

            // 检查数据是否有变化
            bool noChange = false;
            {
                std::lock_guard<std::mutex> lock(userDataMutex);
                noChange = (audio_chunks[clientId].begin() == audio_chunk_begins[clientId] &&
                            audio_chunks[clientId].size() == audio_chunk_lasts[clientId]);
            }

            if (noChange)
            {
                std::lock_guard<std::mutex> lock(userDataMutex);
                if (REPEAT_COUNTS[clientId] > MAX_REPEAT_COUNT)
                {
                    REPEAT_COUNTS[clientId] = 0;
                    if (!last_recognized_texts[clientId].empty())
                    {
                        // 将文本发送到WebSocket客户端
                        if (audioServer != nullptr)
                        {
                            audioServer->sendTextResult(
                                std::regex_replace(last_recognized_texts[clientId], std::regex("\\.\\.\\.$"), "。"),
                                true,
                                clientId);
                        }
                        last_recognized_texts[clientId] = "";
                    }
                }
                else
                {
                    REPEAT_COUNTS[clientId]++;
                }
                continue;
            }

            // 更新处理状态
            {
                std::lock_guard<std::mutex> lock(userDataMutex);
                REPEAT_COUNTS[clientId] = 0;
                audio_chunk_lasts[clientId] = audio_chunks[clientId].size();
                audio_chunk_begins[clientId] = audio_chunks[clientId].begin();
            }

            // 检查是否有足够的样本进行处理
            bool hasSufficientSamples = false;
            {
                std::lock_guard<std::mutex> lock(userDataMutex);
                hasSufficientSamples = audio_chunks[clientId].size() >= SAMPLE_RATE;
            }

            if (hasSufficientSamples)
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
                    wparams.entropy_thold = 1.6f;   // 熵阈值，过高可能导致更多噪声输出，过低可能过于保守
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
                    std::vector<float> audio_copy;
                    {
                        std::lock_guard<std::mutex> lock(bufferMutex);
                        audio_copy = audio_chunks[clientId];
                    }

                    std::string recognized_text;
                    std::string recognized_text_all;
                    bool end = false;
                    float end_time = audio_copy.size() / SAMPLE_RATE * 1000;
                    std::string last_token;

                    if (whisper_full(ctx, wparams, audio_copy.data(), audio_copy.size()) == 0)
                    {

                        // 提取识别结果
                        const int n_segments = whisper_full_n_segments(ctx);

                        for (int i = 0; i < n_segments; ++i)
                        {
                            // 输出每个token的时间戳信息
                            std::string str_token_total;
                            const int n_tokens = whisper_full_n_tokens(ctx, i);
                            recognized_text = "";
                            for (int j = 0; j < n_tokens; ++j)
                            {
                                const int token = whisper_full_get_token_id(ctx, i, j);
                                const char *token_text = whisper_token_to_str(ctx, token);

                                // 获取token的时间戳数据
                                whisper_token_data token_data = whisper_full_get_token_data(ctx, i, j);

                                // 以毫秒为单位输出时间戳
                                float time_start_ms = token_data.t0 * 10.0f;
                                float time_end_ms = token_data.t1 * 10.0f;

                                std::string str_token(token_text);
                                str_token_total += str_token;
                                last_token = str_token_total;
                                if (str_token_total.length() > 3)
                                {
                                    last_token = str_token_total.substr(str_token_total.length() - 3);
                                }
                                if (str_token != "[_BEG_]")
                                {
                                    recognized_text += str_token;
                                }
                                if (str_token == "." || str_token == "!" || str_token == "?" || str_token == "。" || last_token == "。" || last_token == "？")
                                {
                                    end_time = time_end_ms;
                                    if (j > 2 && j < n_tokens - 10)
                                    {
                                        end = true;
                                    }

                                    break;
                                }
                            }

                            const char *text = whisper_full_get_segment_text(ctx, i);
                            if (text)
                            {
                                std::string segment_text(text);
                                // recognized_text += segment_text;
                                recognized_text_all = segment_text;
                            }
                        }

                        // 正则表达式匹配句末句号
                        recognized_text_all = std::regex_replace(recognized_text_all, pattern, "...");

                        // 去除开头的逗号
                        recognized_text = std::regex_replace(recognized_text, pattern_dou, "");
                        recognized_text_all = std::regex_replace(recognized_text_all, pattern_dou, "");

                        // // 去除句号
                        // recognized_text = std::regex_replace(recognized_text, pattern_ju, ".");
                        // recognized_text_all = std::regex_replace(recognized_text_all, pattern_ju, ".");

                        // // 去除感叹号
                        // recognized_text = std::regex_replace(recognized_text, pattern_gan, "!");
                        // recognized_text_all = std::regex_replace(recognized_text_all, pattern_gan, "!");

                        // // 去除问号
                        // recognized_text = std::regex_replace(recognized_text, pattern_wen, "?");
                        // recognized_text_all = std::regex_replace(recognized_text_all, pattern_wen, "?");

                        // // 去除逗号
                        // recognized_text = std::regex_replace(recognized_text, pattern_dd, ",");
                        // recognized_text_all = std::regex_replace(recognized_text_all, pattern_dd, ",");

                        if (recognized_text.empty())
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                            continue;
                        }

                        if (recognized_text_all == ".")
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                            continue;
                        }

                        // 检查是否与上次识别结果相同，避免重复显示
                        if (recognized_text_all != last_recognized_texts[clientId])
                        {
                            // 打印实时识别结果
                            std::cout << "L: " << recognized_text_all << std::endl;

                            // 发送文本到WebSocket客户端
                            if (audioServer != nullptr)
                            {
                                audioServer->sendTextResult(recognized_text_all, false, clientId);
                            }

                            // 更新上次识别结果
                            last_recognized_texts[clientId] = recognized_text_all;
                        }

                        // 如果是完整的句子，则发送完整文本结果
                        if (end)
                        {
                            // 检查是否与上次完整句子相同，避免重复发送
                            if (recognized_text != last_complete_texts[clientId])
                            {
                                std::cout << "T: " << recognized_text << std::endl;

                                // 发送完整句子到WebSocket客户端
                                if (audioServer != nullptr)
                                {
                                    audioServer->sendTextResult(recognized_text, true, clientId);
                                }

                                // 更新上次完整句子
                                last_complete_texts[clientId] = recognized_text;
                            }

                            {
                                // 清空音频数据
                                auto t1 = end_time / 1000 * SAMPLE_RATE;

                                std::lock_guard<std::mutex> lock(bufferMutex);
                                if (audio_chunks[clientId].size() >= t1)
                                {
                                    audio_chunks[clientId].erase(audio_chunks[clientId].begin(), audio_chunks[clientId].begin() + t1);
                                    std::cout << "<KEYWORD> ClientID: " << clientId << std::endl;
                                }
                                else
                                {
                                    audio_chunks[clientId].clear();
                                    std::cout << "<CLEAR> ClientID: " << clientId << std::endl;
                                }

                                // 重置音频处理状态，避免部分音频被重复处理
                                audio_chunk_lasts[clientId] = audio_chunks[clientId].size();
                            }
                        }
                    }

                    // 限制音频缓冲区大小
                    std::lock_guard<std::mutex> lock(bufferMutex);
                    if (audio_chunks[clientId].size() > MAX_AUDIO_LENGTH)
                    {
                        if (!last_recognized_texts[clientId].empty())
                        {
                            // 将文本发送到WebSocket客户端
                            if (audioServer != nullptr)
                            {
                                audioServer->sendTextResult(
                                    std::regex_replace(last_recognized_texts[clientId], std::regex("\\.\\.\\.$"), "。"),
                                    true,
                                    clientId);
                            }
                            last_recognized_texts[clientId] = "";
                        }
                        audio_chunks[clientId].clear();
                        audio_chunk_begins[clientId] = audio_chunks[clientId].begin();
                        std::cout << "<TIME> ClientID: " << clientId << std::endl;
                    }
                }
                catch (const std::exception &e)
                {
                    std::cerr << "处理音频时出错 (ClientID: " << clientId << "): " << e.what() << std::endl;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void processAudioStream()
{
    while (running)
    {
        // 检查队列是否有数据
        if (!audioQueue.empty())
        {
            AudioData data;
            {
                std::lock_guard<std::mutex> lock(audioQueueMutex);
                data = std::move(audioQueue.front());
                audioQueue.pop();
            }

            // 添加到音频缓冲区
            {
                std::lock_guard<std::mutex> lock(bufferMutex);
                audio_chunks[data.clientId].insert(audio_chunks[data.clientId].end(), data.buffer.begin(), data.buffer.end());
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int main(int argc, char **argv)
{

// 设置中文控制台输出
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    // 设置信号处理
    signal(SIGINT, signalHandler);

    std::cout << "启动AutoTalk..." << std::endl;

    // 初始化 SystemMonitor
    systemMonitor = new SystemMonitor();
    systemMonitor->initialize();

    // 初始化 WebSocket 音频服务器
    audioServer = new AudioServer();
    if (!audioServer->initialize("localhost", 3000))
    {
        std::cerr << "初始化音频服务器失败" << std::endl;
        delete audioServer;
        audioServer = nullptr;
        return 1;
    }

    // 加载模型
    std::string modelPath = "models/ggml-small.bin";

    // 检查命令行参数
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--model" && i + 1 < argc)
        {
            modelPath = argv[i + 1];
            i++;
        }
    }

    // 检查模型文件是否存在
    if (!std::filesystem::exists(modelPath))
    {
        std::cerr << "模型文件不存在: " << modelPath << std::endl;
        return 1;
    }

    std::cout << "加载Whisper模型: " << modelPath << std::endl;

    // 初始化 Whisper 上下文
    whisper_context_params cparams;
    cparams.use_gpu = true;
    ctx = whisper_init_from_file_with_params(modelPath.c_str(), cparams);

    if (!ctx)
    {
        std::cerr << "加载模型失败" << std::endl;
        return 1;
    }

    std::cout << "模型加载成功" << std::endl;

    // 启动音频处理
    if (!audioServer->start(processAudio))
    {
        std::cerr << "启动音频处理失败" << std::endl;
        whisper_free(ctx);
        delete audioServer;
        audioServer = nullptr;
        return 1;
    }

    std::cout << "开始接收音频数据..." << std::endl;

    // 初始化全局变量
    audio_chunk_begins[""] = audio_chunks[""].begin();

    // 创建处理线程
    std::thread processThread(processAudioStream);
    std::thread recognitionThread(processSpeechRecognition);

    // 主线程等待，直到收到退出信号
    while (running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 等待线程结束
    processThread.join();
    recognitionThread.join();

    // 清理资源
    if (audioServer)
    {
        audioServer->stop();
        delete audioServer;
        audioServer = nullptr;
    }

    if (ctx)
    {
        whisper_free(ctx);
        ctx = nullptr;
    }

    if (systemMonitor)
    {
        delete systemMonitor;
        systemMonitor = nullptr;
    }

    std::cout << "程序已退出" << std::endl;

    return 0;
}