<template>
  <div class="container">
    <h1>AutoTalk 语音识别客户端</h1>
    
    <div class="status" :class="{ connected: isConnected, disconnected: !isConnected }">
      服务器状态: {{ isConnected ? '已连接' : '未连接' }}
    </div>
    
    <div class="form-group">
      <label for="server-address">服务器地址</label>
      <input 
        type="text" 
        id="server-address" 
        v-model="serverAddress" 
        placeholder="例如: http://localhost:3000"
        :disabled="isConnected"
      />
    </div>
    
    <div class="form-group">
      <label for="audio-device">音频输入设备</label>
      <select id="audio-device" v-model="selectedDevice" :disabled="!isConnected || isRecording">
        <option value="">请选择设备</option>
        <option v-for="device in audioDevices" :key="device.deviceId" :value="device.deviceId">
          {{ device.label }}
        </option>
      </select>
    </div>
    
    <div class="controls">
      <button @click="connectToServer" v-if="!isConnected">连接服务器</button>
      <button @click="disconnectFromServer" v-else>断开连接</button>
      
      <button 
        @click="toggleRecording" 
        :disabled="!isConnected || !selectedDevice" 
        class="record-button"
      >
        {{ isRecording ? '停止识别' : '开始识别' }}
      </button>
    </div>
    
    <div class="recognition-container">
      <div v-if="recognitionResults.length === 0" class="empty-state">
        识别结果将显示在这里...
      </div>
      <div 
        v-for="(result, index) in recognitionResults" 
        :key="index" 
        class="recognition-item"
        :class="{ 'live': result.type === 'live', 'complete': result.type === 'complete' }"
      >
        <div>{{ result.text }}</div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue';
import { io, Socket } from 'socket.io-client';

// 声明全局类型
declare global {
  interface Window {
    electronAPI?: {
      getAudioDevices: () => Promise<any[]>;
    };
  }
}

// 状态
const isConnected = ref(false);
const isRecording = ref(false);
const serverAddress = ref('http://localhost:3000');
const audioDevices = ref<MediaDeviceInfo[]>([]);
const selectedDevice = ref('');
const socketRef = ref<Socket | null>(null);
const mediaStreamRef = ref<MediaStream | null>(null);
const audioContextRef = ref<AudioContext | null>(null);
const processorRef = ref<ScriptProcessorNode | null>(null);

// 识别结果，包含类型（实时/完整）和文本内容
interface RecognitionResult {
  type: 'live' | 'complete';
  text: string;
}

const recognitionResults = ref<RecognitionResult[]>([
  { type: 'live', text: '' } // 初始空结果
]);

// 连接服务器
const connectToServer = () => {
  try {
    const socket = io(serverAddress.value);
    
    socket.on('connect', () => {
      console.log('已连接到服务器');
      isConnected.value = true;
      socketRef.value = socket;
      
      // 加载音频设备
      loadAudioDevices();
    });
    
    socket.on('disconnect', () => {
      console.log('与服务器断开连接');
      isConnected.value = false;
      stopRecording();
    });
    
    socket.on('server_ready', (message) => {
      console.log('服务器就绪:', message);
    });
    
    socket.on('recognition_result', (result) => {
      console.log('收到识别结果:', result);
      processRecognitionResult(result);
    });
    
    socket.on('connect_error', (err) => {
      console.error('连接错误:', err);
      alert(`连接服务器失败: ${err.message}`);
    });
  } catch (error) {
    console.error('连接服务器时出错:', error);
    alert(`连接服务器时出错: ${error}`);
  }
};

// 断开服务器连接
const disconnectFromServer = () => {
  if (socketRef.value) {
    stopRecording();
    socketRef.value.disconnect();
    socketRef.value = null;
    isConnected.value = false;
  }
};

// 处理识别结果
const processRecognitionResult = (result: string) => {
  if (result.startsWith('L:')) {
    // 实时识别结果，更新当前结果列表的第一项
    const liveText = result.substring(2);
    if (recognitionResults.value.length > 0 && recognitionResults.value[0].type === 'live') {
      recognitionResults.value[0].text = liveText;
    } else {
      // 如果第一项不是实时结果，添加一个新的实时结果项
      recognitionResults.value.unshift({ type: 'live', text: liveText });
    }
  } else if (result.startsWith('T:')) {
    // 完整识别结果，添加到结果列表的开头，并创建新的实时结果项
    const completeText = result.substring(2);
    
    // 如果第一项是实时结果，将其转换为完整结果
    if (recognitionResults.value.length > 0 && recognitionResults.value[0].type === 'live') {
      recognitionResults.value[0].type = 'complete';
      recognitionResults.value[0].text = completeText;
    } else {
      // 否则，添加一个新的完整结果项
      recognitionResults.value.unshift({ type: 'complete', text: completeText });
    }
    
    // 在开头添加一个新的空实时结果项
    recognitionResults.value.unshift({ type: 'live', text: '' });
  }
};

// 加载音频设备
const loadAudioDevices = async () => {
  try {
    // 先尝试使用Web API获取设备列表
    const devices = await navigator.mediaDevices.enumerateDevices();
    audioDevices.value = devices.filter(device => device.kind === 'audioinput');
    
    // 如果没有设备，可能是因为权限问题，请求权限
    if (audioDevices.value.length === 0) {
      // 请求麦克风权限
      await navigator.mediaDevices.getUserMedia({ audio: true });
      // 重新获取设备列表
      const devicesAfterPermission = await navigator.mediaDevices.enumerateDevices();
      audioDevices.value = devicesAfterPermission.filter(device => device.kind === 'audioinput');
    }
  } catch (error) {
    console.error('加载音频设备失败:', error);
    alert(`加载音频设备失败: ${error}`);
  }
};

// 开始/停止录制
const toggleRecording = () => {
  if (isRecording.value) {
    stopRecording();
  } else {
    startRecording();
  }
};

// 开始录制
const startRecording = async () => {
  if (!socketRef.value || !selectedDevice.value) return;
  
  try {
    // 获取音频流
    const stream = await navigator.mediaDevices.getUserMedia({
      audio: {
        deviceId: { exact: selectedDevice.value },
        echoCancellation: true,
        noiseSuppression: true,
        autoGainControl: true
      }
    });
    
    mediaStreamRef.value = stream;
    
    // 创建音频上下文
    const audioContext = new (window.AudioContext || (window as any).webkitAudioContext)({
      sampleRate: 16000 // 设置为服务器期望的采样率
    });
    audioContextRef.value = audioContext;
    
    // 创建音频源
    const source = audioContext.createMediaStreamSource(stream);
    
    // 创建处理节点
    const processor = audioContext.createScriptProcessor(4096, 1, 1);
    processorRef.value = processor;
    
    // 处理音频数据
    processor.onaudioprocess = (e) => {
      if (socketRef.value && isRecording.value) {
        const inputData = e.inputBuffer.getChannelData(0);
        
        // 转换为Float32Array以通过Socket发送
        const audioData = new Float32Array(inputData.length);
        for (let i = 0; i < inputData.length; i++) {
          audioData[i] = inputData[i];
        }
        
        // 发送音频数据到服务器
        socketRef.value.emit('audio_data', Array.from(audioData));
      }
    };
    
    // 连接节点
    source.connect(processor);
    processor.connect(audioContext.destination);
    
    isRecording.value = true;
  } catch (error) {
    console.error('启动录音失败:', error);
    alert(`启动录音失败: ${error}`);
  }
};

// 停止录制
const stopRecording = () => {
  if (mediaStreamRef.value) {
    // 停止所有音频轨道
    mediaStreamRef.value.getTracks().forEach(track => track.stop());
    mediaStreamRef.value = null;
  }
  
  if (processorRef.value && audioContextRef.value) {
    processorRef.value.disconnect();
    processorRef.value = null;
  }
  
  if (audioContextRef.value) {
    audioContextRef.value.close().catch(err => console.error('关闭音频上下文错误:', err));
    audioContextRef.value = null;
  }
  
  isRecording.value = false;
};

// 组件挂载
onMounted(() => {
  // 如果在Electron环境中，获取系统音频设备
  if (window.electronAPI) {
    window.electronAPI.getAudioDevices()
      .then(devices => {
        console.log('Electron音频设备:', devices);
      })
      .catch(err => {
        console.error('获取Electron音频设备失败:', err);
      });
  }
});

// 组件卸载
onUnmounted(() => {
  disconnectFromServer();
});
</script>

<style scoped>
.container {
  padding: 20px;
}

.controls {
  display: flex;
  gap: 10px;
  margin-bottom: 20px;
}

.record-button {
  margin-left: auto;
}

.empty-state {
  color: #999;
  text-align: center;
  padding: 20px;
}

input {
  width: 100%;
  padding: 10px;
  border: 1px solid var(--border-color);
  border-radius: 4px;
  margin-bottom: 20px;
  font-size: 16px;
}
</style> 