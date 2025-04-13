#pragma once

#include <vector>
#include <functional>
#include <string>
#include <thread>
#include "portaudio.h"
#ifdef _WIN32
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#endif

class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();

    bool initialize();
    bool start(std::function<void(const std::vector<float>&)> callback);
    void stop();
    
    // 获取可用的输入设备列表
    std::vector<std::pair<int, std::string>> getInputDevices() const;
    
    // 设置输入设备
    bool setInputDevice(int deviceIndex);

    // 设置是否使用环回捕获
    void setLoopbackCapture(bool enable);

    // 设置音频增益
    void setGain(float gain);

    // 获取当前增益值
    float getGain() const;

private:
    static int paCallback(const void* inputBuffer, void* outputBuffer,
                         unsigned long framesPerBuffer,
                         const PaStreamCallbackTimeInfo* timeInfo,
                         PaStreamCallbackFlags statusFlags,
                         void* userData);

    static bool comInitialized_;  // 添加静态成员变量
    PaStream* stream_;
    std::function<void(const std::vector<float>&)> callback_;
    bool initialized_;
    int currentDeviceIndex_;
    std::vector<float> audioBuffer_;  // 预分配的音频缓冲区
    bool useLoopback_;  // 是否使用环回捕获
    float gain_;  // 音频增益

#ifdef _WIN32
    // WASAPI相关变量
    IMMDeviceEnumerator* pEnumerator_;
    IMMDevice* pDevice_;
    IAudioClient* pAudioClient_;
    IAudioCaptureClient* pCaptureClient_;
    WAVEFORMATEX* pFormat_;
    HANDLE hEvent_;
    std::thread captureThread_;
    bool isCapturing_;
#endif
}; 