#pragma once

#include <vector>
#include <functional>
#include <string>
#include "portaudio.h"

#ifdef _WIN32
#include <windows.h>
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
    // 设置要使用的输入设备
    bool setInputDevice(int deviceIndex);
    // 设置是否使用回环捕获
    void setLoopbackCapture(bool enable);

private:
    static int paCallback(const void* inputBuffer, void* outputBuffer,
                         unsigned long framesPerBuffer,
                         const PaStreamCallbackTimeInfo* timeInfo,
                         PaStreamCallbackFlags statusFlags,
                         void* userData);

    PaStream* stream_;
    std::function<void(const std::vector<float>&)> callback_;
    bool initialized_;
    int currentDeviceIndex_;  // 当前选定的设备索引
    std::vector<float> audioBuffer_;  // 音频数据缓冲区
    bool useLoopback_;  // 是否使用回环捕获
    float gain_;  // 音频增益

#ifdef _WIN32
    // Windows 特定的成员变量
    IMMDeviceEnumerator* pEnumerator_;
    IMMDevice* pDevice_;
    IAudioClient* pAudioClient_;
    IAudioCaptureClient* pCaptureClient_;
    WAVEFORMATEX* pFormat_;
    HANDLE hEvent_;
    bool isCapturing_;
    static bool comInitialized_;  // COM初始化状态
#endif
}; 