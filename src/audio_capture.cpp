#include "audio_capture.h"
#include <portaudio.h>
#ifndef _WIN32
#include <pa_linux_alsa.h>
#endif
#include <cstring>
#include <iostream>
#include <set>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cmath>

#ifdef _WIN32
bool AudioCapture::comInitialized_ = false;
#endif

AudioCapture::AudioCapture() 
    : stream_(nullptr)
    , initialized_(false)
    , currentDeviceIndex_(-1)
    , audioBuffer_(512)  // 预分配缓冲区
    , useLoopback_(false)
    , gain_(2.0f)  // 默认增益为2.0
#ifdef _WIN32
    , pEnumerator_(nullptr)
    , pDevice_(nullptr)
    , pAudioClient_(nullptr)
    , pCaptureClient_(nullptr)
    , pFormat_(nullptr)
    , hEvent_(nullptr)
    , isCapturing_(false)
#endif
{
#ifdef _WIN32
    if (!comInitialized_) {
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(hr)) {
            comInitialized_ = true;
        } else if (hr == RPC_E_CHANGED_MODE) {
            // 如果已经在其他线程以不同的模式初始化，尝试多线程模式
            hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
            if (SUCCEEDED(hr)) {
                comInitialized_ = true;
            }
        }
        
        if (!comInitialized_) {
            std::cerr << "COM 初始化失败，错误代码: 0x" << std::hex << hr << std::endl;
        }
    }
#endif
}

AudioCapture::~AudioCapture() {
    stop();
#ifdef _WIN32
    if (pFormat_) {
        CoTaskMemFree(pFormat_);
    }
    if (pEnumerator_) {
        pEnumerator_->Release();
    }
    if (pDevice_) {
        pDevice_->Release();
    }
    if (pAudioClient_) {
        pAudioClient_->Release();
    }
    if (pCaptureClient_) {
        pCaptureClient_->Release();
    }
    if (hEvent_) {
        CloseHandle(hEvent_);
    }
    if (comInitialized_) {
        CoUninitialize();
        comInitialized_ = false;
    }
#endif
}

bool AudioCapture::initialize() {
    if (initialized_) {
        return true;
    }
    
    // 初始化PortAudio
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio 初始化失败: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

#ifndef _WIN32
    // Linux特定的音频配置
    const char* hostApiName = "ALSA";
    PaHostApiIndex hostApiIndex = Pa_HostApiTypeIdToHostApiIndex(paALSA);
    if (hostApiIndex < 0) {
        std::cerr << "未找到ALSA音频API" << std::endl;
        Pa_Terminate();
        return false;
    }

    const PaHostApiInfo* hostApiInfo = Pa_GetHostApiInfo(hostApiIndex);
    if (!hostApiInfo) {
        std::cerr << "无法获取ALSA API信息" << std::endl;
        Pa_Terminate();
        return false;
    }
#endif

    // 检查默认输入设备
    PaDeviceIndex defaultInput = Pa_GetDefaultInputDevice();
    if (defaultInput == paNoDevice) {
        std::cerr << "未找到默认输入设备" << std::endl;
        Pa_Terminate();
        return false;
    }

    // 获取默认设备信息
    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(defaultInput);
    if (!deviceInfo) {
        std::cerr << "无法获取默认设备信息" << std::endl;
        Pa_Terminate();
        return false;
    }

    // 检查设备是否支持输入
    if (deviceInfo->maxInputChannels == 0) {
        std::cerr << "默认设备不支持输入" << std::endl;
        Pa_Terminate();
        return false;
    }

#ifdef _WIN32
    // Windows特定的初始化代码
    if (!comInitialized_) {
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(hr)) {
            comInitialized_ = true;
        } else if (hr == RPC_E_CHANGED_MODE) {
            hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
            if (SUCCEEDED(hr)) {
                comInitialized_ = true;
            }
        }
        
        if (!comInitialized_) {
            std::cerr << "COM 初始化失败，错误代码: 0x" << std::hex << hr << std::endl;
            Pa_Terminate();
            return false;
        }
    }

    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        NULL,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&pEnumerator_);
    if (FAILED(hr)) {
        std::cerr << "创建设备枚举器失败" << std::endl;
        Pa_Terminate();
        return false;
    }
#endif

    initialized_ = true;
    return true;
}

std::vector<std::pair<int, std::string>> AudioCapture::getInputDevices() const {
    std::vector<std::pair<int, std::string>> devices;
    if (!initialized_) {
        std::cerr << "AudioCapture 未初始化" << std::endl;
        return devices;
    }

    int numDevices = Pa_GetDeviceCount();
    if (numDevices < 0) {
        std::cerr << "获取设备数量失败: " << Pa_GetErrorText(numDevices) << std::endl;
        return devices;
    }

    // 用于存储已处理的设备名称
    std::vector<std::string> processedNames;

    // devices.push_back({0, "默认设备"});
    // processedNames.push_back("默认设备");
    
    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);

        devices.push_back({i, deviceInfo->name});
        processedNames.push_back(deviceInfo->name);

        // if (deviceInfo->maxInputChannels > 0) {
        //     std::string deviceName = deviceInfo->name;
            
        //     // 检查是否是麦克风设备
        //     if (deviceName.find("麦克风") != std::string::npos || 
        //         deviceName.find("Microphone") != std::string::npos || 
        //         deviceName.find("input") != std::string::npos) {
                
        //         // 检查是否与已处理的设备名称相似
        //         bool isDuplicate = false;
        //         for (const auto& processedName : processedNames) {
        //             // 如果新设备名称包含已处理名称，或者已处理名称包含新设备名称
        //             if (deviceName.find(processedName) != std::string::npos || 
        //                 processedName.find(deviceName) != std::string::npos) {
        //                 isDuplicate = true;
        //                 break;
        //             }
        //         }
                
        //         if (!isDuplicate) {
        //             devices.push_back({i, deviceName});
        //             processedNames.push_back(deviceName);
        //         }
        //     }
        // }
    }

    return devices;
}

bool AudioCapture::setInputDevice(int deviceIndex) {
    if (!initialized_) {
        std::cerr << "AudioCapture 未初始化" << std::endl;
        return false;
    }

    int numDevices = Pa_GetDeviceCount();
    if (deviceIndex < 0 || deviceIndex >= numDevices) {
        std::cerr << "设备索引无效" << std::endl;
        return false;
    }

    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(deviceIndex);
    if (deviceInfo->maxInputChannels == 0) {
        std::cerr << "所选设备不是输入设备" << std::endl;
        return false;
    }

    currentDeviceIndex_ = deviceIndex;
    return true;
}

bool AudioCapture::start(std::function<void(const std::vector<float>&)> callback) {
    if (!initialized_) {
        std::cerr << "错误: 音频捕获未初始化" << std::endl;
        return false;
    }

    callback_ = callback;

    PaError err;
    PaStreamParameters inputParameters;
    inputParameters.device = (currentDeviceIndex_ >= 0) ? currentDeviceIndex_ : Pa_GetDefaultInputDevice();
    inputParameters.channelCount = 1;
    inputParameters.sampleFormat = paFloat32;
    inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = nullptr;

#ifndef _WIN32
    // Linux特定的音频流配置
    PaAlsaStreamInfo* alsaStreamInfo = new PaAlsaStreamInfo;
    alsaStreamInfo->size = sizeof(PaAlsaStreamInfo);
    alsaStreamInfo->hostApiType = paALSA;
    alsaStreamInfo->version = 1;
    alsaStreamInfo->deviceString = nullptr; // 使用默认设备
    inputParameters.hostApiSpecificStreamInfo = alsaStreamInfo;
#endif

    err = Pa_OpenStream(
        &stream_,
        &inputParameters,
        nullptr,
        16000,
        512,
        paClipOff,
        paCallback,
        this
    );

#ifndef _WIN32
    delete alsaStreamInfo;
#endif

    if (err != paNoError) {
        std::cerr << "错误: 无法打开音频流: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    err = Pa_StartStream(stream_);
    if (err != paNoError) {
        std::cerr << "错误: 无法启动音频流: " << Pa_GetErrorText(err) << std::endl;
        Pa_CloseStream(stream_);
        return false;
    }

    return true;
}

void AudioCapture::stop() {
    if (stream_) {
        Pa_StopStream(stream_);
        Pa_CloseStream(stream_);
        stream_ = nullptr;
    }
}

int AudioCapture::paCallback(
    const void* inputBuffer,
    void* outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void* userData
) {
    AudioCapture* self = static_cast<AudioCapture*>(userData);
    const float* in = static_cast<const float*>(inputBuffer);

    if (in != nullptr) {
        // 确保缓冲区大小足够
        if (self->audioBuffer_.size() < framesPerBuffer) {
            self->audioBuffer_.resize(framesPerBuffer);
        }
        
        // 应用增益控制
        float gain = self->gain_;
        for (unsigned long i = 0; i < framesPerBuffer; ++i) {
            self->audioBuffer_[i] = in[i] * gain;
        }
        
        // 调用回调函数
        self->callback_(std::vector<float>(self->audioBuffer_.begin(), self->audioBuffer_.begin() + framesPerBuffer));
    }

    return paContinue;
}

void AudioCapture::setLoopbackCapture(bool enable) {
    useLoopback_ = enable;
}