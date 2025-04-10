#include "audio_recorder.h"
#include <iostream>
#include <cstring>
#include <string>  // 添加string头文件支持std::to_string

// 声明外部日志函数
extern void logMessage(const std::string& message);

AudioRecorder::AudioRecorder() 
    : stream_(nullptr), sampleRate_(16000), framesPerBuffer_(512), isRecording_(false) {
    logMessage("AudioRecorder构造函数");
}

AudioRecorder::~AudioRecorder() {
    logMessage("AudioRecorder析构函数");
    stop();
    Pa_Terminate();
}

bool AudioRecorder::init(int sampleRate, int framesPerBuffer) {
    sampleRate_ = sampleRate;
    framesPerBuffer_ = framesPerBuffer;
    
    logMessage("初始化PortAudio, 采样率=" + std::to_string(sampleRate_) + ", 缓冲区大小=" + std::to_string(framesPerBuffer_));
    
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio初始化失败: " << Pa_GetErrorText(err) << std::endl;
        logMessage("PortAudio初始化失败: " + std::string(Pa_GetErrorText(err)));
        return false;
    }
    
    // 输出可用音频设备信息
    int numDevices = Pa_GetDeviceCount();
    if (numDevices < 0) {
        std::cerr << "获取音频设备数量失败: " << Pa_GetErrorText(numDevices) << std::endl;
        logMessage("获取音频设备数量失败: " + std::string(Pa_GetErrorText(numDevices)));
        return false;
    }
    
    std::cout << "发现 " << numDevices << " 个音频设备" << std::endl;
    logMessage("发现 " + std::to_string(numDevices) + " 个音频设备");
    
    // 列出所有设备
    logMessage("--- 设备列表 ---");
    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
        if (deviceInfo) {
            logMessage("设备 #" + std::to_string(i) + ": " + std::string(deviceInfo->name));
            logMessage("  最大输入通道数: " + std::to_string(deviceInfo->maxInputChannels));
            logMessage("  最大输出通道数: " + std::to_string(deviceInfo->maxOutputChannels));
            logMessage("  默认采样率: " + std::to_string(deviceInfo->defaultSampleRate));
        }
    }
    logMessage("---------------");
    
    int defaultInputDevice = Pa_GetDefaultInputDevice();
    if (defaultInputDevice == paNoDevice) {
        std::cerr << "未找到默认输入设备！请确保麦克风已连接并设置为默认输入设备。" << std::endl;
        logMessage("未找到默认输入设备！请确保麦克风已连接并设置为默认输入设备。");
        return false;
    }
    
    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(defaultInputDevice);
    if (deviceInfo) {
        std::cout << "使用默认输入设备: " << deviceInfo->name << std::endl;
        std::cout << "  最大输入通道数: " << deviceInfo->maxInputChannels << std::endl;
        std::cout << "  默认采样率: " << deviceInfo->defaultSampleRate << std::endl;
        
        logMessage("使用默认输入设备: " + std::string(deviceInfo->name));
        logMessage("  最大输入通道数: " + std::to_string(deviceInfo->maxInputChannels));
        logMessage("  默认采样率: " + std::to_string(deviceInfo->defaultSampleRate));
        
        if (deviceInfo->maxInputChannels <= 0) {
            std::cerr << "所选设备没有输入通道！请选择一个有效的输入设备。" << std::endl;
            logMessage("所选设备没有输入通道！请选择一个有效的输入设备。");
            return false;
        }
    }
    
    return true;
}

bool AudioRecorder::start(AudioCallback callback) {
    if (isRecording_) {
        logMessage("已经在录音中，无法重新开始");
        return false;
    }
    
    callback_ = callback;
    
    PaStreamParameters inputParameters;
    memset(&inputParameters, 0, sizeof(inputParameters));
    
    inputParameters.device = Pa_GetDefaultInputDevice();
    if (inputParameters.device == paNoDevice) {
        std::cerr << "未找到默认输入设备!" << std::endl;
        logMessage("未找到默认输入设备!");
        return false;
    }
    
    inputParameters.channelCount = 1;
    inputParameters.sampleFormat = paFloat32;
    inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = nullptr;
    
    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(inputParameters.device);
    logMessage("打开音频流，设备=" + std::string(deviceInfo->name));
    std::cout << "打开音频流..." << std::endl;
    
    PaError err = Pa_OpenStream(
        &stream_,
        &inputParameters,
        nullptr,
        sampleRate_,
        framesPerBuffer_,
        paClipOff,
        paCallback,
        this
    );
    
    if (err != paNoError) {
        std::cerr << "打开音频流失败: " << Pa_GetErrorText(err) << std::endl;
        logMessage("打开音频流失败: " + std::string(Pa_GetErrorText(err)));
        return false;
    }
    
    std::cout << "启动音频流..." << std::endl;
    logMessage("启动音频流...");
    
    err = Pa_StartStream(stream_);
    if (err != paNoError) {
        std::cerr << "启动音频流失败: " << Pa_GetErrorText(err) << std::endl;
        logMessage("启动音频流失败: " + std::string(Pa_GetErrorText(err)));
        return false;
    }
    
    isRecording_ = true;
    std::cout << "音频录制已启动" << std::endl;
    logMessage("音频录制已启动");
    return true;
}

void AudioRecorder::stop() {
    if (!isRecording_) {
        return;
    }
    
    isRecording_ = false;
    
    if (stream_) {
        std::cout << "停止音频流..." << std::endl;
        logMessage("停止音频流...");
        
        PaError err = Pa_StopStream(stream_);
        if (err != paNoError) {
            std::cerr << "停止音频流失败: " << Pa_GetErrorText(err) << std::endl;
            logMessage("停止音频流失败: " + std::string(Pa_GetErrorText(err)));
        }
        
        err = Pa_CloseStream(stream_);
        if (err != paNoError) {
            std::cerr << "关闭音频流失败: " << Pa_GetErrorText(err) << std::endl;
            logMessage("关闭音频流失败: " + std::string(Pa_GetErrorText(err)));
        }
        
        stream_ = nullptr;
        std::cout << "音频录制已停止" << std::endl;
        logMessage("音频录制已停止");
    }
}

bool AudioRecorder::isRecording() const {
    return isRecording_;
}

int AudioRecorder::paCallback(const void* inputBuffer, void* outputBuffer,
                              unsigned long framesPerBuffer,
                              const PaStreamCallbackTimeInfo* timeInfo,
                              PaStreamCallbackFlags statusFlags,
                              void* userData) {
    AudioRecorder* recorder = static_cast<AudioRecorder*>(userData);
    const float* in = static_cast<const float*>(inputBuffer);
    
    if (inputBuffer == nullptr || !recorder->isRecording_) {
        // 缓冲区为空或者已经停止录音
        if (inputBuffer == nullptr) {
            logMessage("音频回调：输入缓冲区为空");
        }
        return paContinue;
    }
    
    // 复制音频数据
    std::vector<float> audioData(in, in + framesPerBuffer);
    
    // 检查音频数据是否有效
    /*
    float sum = 0.0f;
    for (const auto& sample : audioData) {
        sum += std::abs(sample);
    }
    float average = sum / audioData.size();
    
    // 记录音频电平，用于调试
    static int callbackCounter = 0;
    if (++callbackCounter % 100 == 0) {  // 每100次回调记录一次
        logMessage("音频回调：帧数=" + std::to_string(framesPerBuffer) + ", 平均电平=" + std::to_string(average));
        callbackCounter = 0;
    }
    */
    
    // 调用回调函数处理音频数据
    recorder->callback_(audioData);
    
    return paContinue;
} 