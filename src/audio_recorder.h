#pragma once

#include <portaudio.h>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>
#include <condition_variable>

class AudioRecorder {
public:
    using AudioCallback = std::function<void(const std::vector<float>&)>;

    AudioRecorder();
    ~AudioRecorder();

    // 初始化音频录制器
    bool init(int sampleRate = 16000, int framesPerBuffer = 512);
    
    // 开始录音
    bool start(AudioCallback callback);
    
    // 停止录音
    void stop();
    
    // 获取当前是否正在录音
    bool isRecording() const;

private:
    static int paCallback(const void* inputBuffer, void* outputBuffer, 
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo* timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void* userData);

    PaStream* stream_;
    int sampleRate_;
    int framesPerBuffer_;
    AudioCallback callback_;
    std::atomic<bool> isRecording_;
    std::vector<float> buffer_;
    std::mutex mutex_;
    std::condition_variable cv_;
}; 