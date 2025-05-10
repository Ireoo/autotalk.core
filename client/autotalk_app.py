import sys
import json
import numpy as np
import pyaudio
import websocket
import threading
import time
from PyQt5.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                            QHBoxLayout, QLabel, QComboBox, QPushButton, 
                            QLineEdit, QSpinBox, QTextEdit, QGroupBox, QCheckBox, QListWidget)
from PyQt5.QtGui import QPainter, QColor, QPen
from PyQt5.QtCore import Qt, QTimer, pyqtSignal, QObject

class AudioVisualizer(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumHeight(100)
        self.data = np.zeros(100)
        
    def update_data(self, data):
        self.data = data
        self.update()
        
    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        
        # 设置背景
        painter.fillRect(self.rect(), Qt.black)
        
        if len(self.data) == 0:
            return
            
        # 绘制频谱图
        width = self.width()
        height = self.height()
        bar_width = width / len(self.data)
        
        for i, value in enumerate(self.data):
            # 归一化值
            normalized = min(1.0, max(0.0, value))
            bar_height = normalized * height
            
            # 根据频率创建渐变颜色
            r = min(255, int(50 + (normalized * 200)))
            g = min(255, int(50 + (normalized * 100)))
            b = 155
            
            painter.setPen(Qt.NoPen)
            painter.setBrush(QColor(r, g, b))
            
            x = i * bar_width
            y = height - bar_height
            painter.drawRect(int(x), int(y), int(bar_width), int(bar_height))

class WebSocketClient(QObject):
    message_received = pyqtSignal(str)
    connection_status = pyqtSignal(bool, str)
    
    def __init__(self):
        super().__init__()
        self.ws = None
        self.connected = False
        
    def connect_to_server(self, host, port):
        if self.connected:
            return
            
        try:
            url = f"ws://{host}:{port}/"
            self.ws = websocket.WebSocketApp(
                url,
                on_open=self._on_open,
                on_message=self._on_message,
                on_error=self._on_error,
                on_close=self._on_close
            )
            
            # 在新线程中启动WebSocket
            threading.Thread(target=self.ws.run_forever, daemon=True).start()
            return True
        except Exception as e:
            self.connection_status.emit(False, f"连接失败: {str(e)}")
            return False
    
    def disconnect(self):
        if self.ws and self.connected:
            self.ws.close()
    
    def send_data(self, data):
        if not self.ws or not self.connected:
            return False
        
        try:
            # 尝试将NumPy数组转为列表再发送
            if isinstance(data, np.ndarray):
                data = data.tolist()
            
            # 确保转换为ASCII安全的JSON字符串，使用ensure_ascii=True避免Unicode编码问题
            json_data = json.dumps(data, ensure_ascii=True)
            self.ws.send(json_data)
            return True
        except Exception as e:
            self.connection_status.emit(False, f"发送数据失败: {str(e)}")
            return False
    
    def _on_open(self, ws):
        self.connected = True
        self.connection_status.emit(True, "已成功连接到服务器！")
    
    def _on_message(self, ws, message):
        self.message_received.emit(message)
    
    def _on_error(self, ws, error):
        self.connection_status.emit(False, f"连接错误: {str(error)}")
    
    def _on_close(self, ws, close_status_code, close_msg):
        self.connected = False
        self.connection_status.emit(False, "连接已关闭")

class AudioHandler:
    def __init__(self, callback=None):
        self.callback = callback
        self.p = pyaudio.PyAudio()
        self.stream = None
        self.recording = False
        self.buffer = []
        self.auto_send = False
        self.send_callback = None
        self.send_thread = None
        self.stop_thread = False
        
    def get_devices(self):
        input_devices = []
        output_devices = []
        
        for i in range(self.p.get_device_count()):
            device_info = self.p.get_device_info_by_index(i)
            try:
                # 尝试确保设备名称是正确的编码格式
                if isinstance(device_info['name'], bytes):
                    device_name = device_info['name'].decode('utf-8', errors='replace')
                else:
                    device_name = str(device_info['name']).strip()
                
                # 如果设备名为空，使用索引号代替
                if not device_name:
                    device_name = f"设备 {i}"
                
                if device_info['maxInputChannels'] > 0:
                    input_devices.append((i, device_name))
                    
                if device_info['maxOutputChannels'] > 0:
                    output_devices.append((i, device_name))
            except Exception as e:
                # 如果解析设备名称出错，使用索引号作为设备名
                if device_info['maxInputChannels'] > 0:
                    input_devices.append((i, f"输入设备 {i}"))
                if device_info['maxOutputChannels'] > 0:
                    output_devices.append((i, f"输出设备 {i}"))
        
        return input_devices, output_devices
    
    def start_recording(self, device_id):
        if self.recording:
            return
            
        self.buffer = []
        self.recording = True
        
        # 开始录音
        self.stream = self.p.open(
            format=pyaudio.paFloat32,
            channels=1,
            rate=16000,
            input=True,
            input_device_index=device_id,
            frames_per_buffer=1024,
            stream_callback=self._audio_callback
        )
    
    def stop_recording(self):
        if not self.recording:
            return
            
        self.recording = False
        if self.stream:
            self.stream.stop_stream()
            self.stream.close()
            self.stream = None
    
    def _audio_callback(self, in_data, frame_count, time_info, status):
        # 将音频数据转换为NumPy数组
        data = np.frombuffer(in_data, dtype=np.float32)
        
        # 存储数据
        self.buffer.extend(data.tolist())
        
        # 限制缓冲区大小
        if len(self.buffer) > 48000:  # 约3秒的音频
            self.buffer = self.buffer[-48000:]
        
        # 如果有回调函数，将数据传给回调
        if self.callback:
            # 计算频谱
            fft_data = np.abs(np.fft.rfft(data))
            fft_data = fft_data / np.max(fft_data) if np.max(fft_data) > 0 else fft_data
            
            # 降低分辨率以便显示
            reduced_data = fft_data[0:100]
            
            self.callback(reduced_data)
        
        return (in_data, pyaudio.paContinue)
    
    def get_audio_data(self):
        return np.array(self.buffer, dtype=np.float32)
    
    def start_auto_send(self, interval, callback):
        """开始自动发送音频数据"""
        if self.send_thread:
            return
        self.auto_send = True
        self.send_callback = callback
        self.stop_thread = False
        
        # 启动发送线程
        self.send_thread = threading.Thread(target=self._auto_send_thread, args=(interval,))
        self.send_thread.daemon = True
        self.send_thread.start()
    
    def stop_auto_send(self):
        """停止自动发送音频数据"""
        self.auto_send = False
        self.stop_thread = True
        if self.send_thread:
            self.send_thread = None
    
    def _auto_send_thread(self, interval):
        """自动发送线程函数"""
        while self.recording and self.auto_send and not self.stop_thread:
            if self.send_callback:
                # 获取音频数据
                audio_data = self.get_audio_data()
                if len(audio_data) > 0:
                    self.send_callback(audio_data)
            # 将秒转换为毫秒，使用sleep
            time.sleep(interval / 1000.0)
    
    def __del__(self):
        self.stop_recording()
        self.p.terminate()

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        
        # 初始化
        self.ws_client = WebSocketClient()
        self.audio_handler = AudioHandler(self.update_visualizer)
        
        # 设置窗口属性
        self.setWindowTitle("AutoTalk - 语音识别应用")
        self.setMinimumSize(800, 600)
        
        # 创建UI
        self._create_ui()
        
        # 连接信号
        self._connect_signals()
        
        # 加载设备列表
        self._load_audio_devices()
    
    def _create_ui(self):
        central_widget = QWidget()
        main_layout = QVBoxLayout(central_widget)
        
        # 服务器连接部分
        server_group = QGroupBox("服务器连接设置")
        server_layout = QVBoxLayout(server_group)
        
        server_host_layout = QHBoxLayout()
        server_host_layout.addWidget(QLabel("服务器地址:"))
        self.server_host = QLineEdit("127.0.0.1")
        server_host_layout.addWidget(self.server_host)
        
        server_port_layout = QHBoxLayout()
        server_port_layout.addWidget(QLabel("端口:"))
        self.server_port = QSpinBox()
        self.server_port.setRange(1, 65535)
        self.server_port.setValue(3000)
        server_port_layout.addWidget(self.server_port)
        
        server_buttons_layout = QHBoxLayout()
        self.connect_button = QPushButton("连接服务器")
        self.disconnect_button = QPushButton("断开连接")
        self.disconnect_button.setEnabled(False)
        server_buttons_layout.addWidget(self.connect_button)
        server_buttons_layout.addWidget(self.disconnect_button)
        
        self.connection_status = QLabel("应用已成功加载")
        
        server_layout.addLayout(server_host_layout)
        server_layout.addLayout(server_port_layout)
        server_layout.addLayout(server_buttons_layout)
        server_layout.addWidget(self.connection_status)
        
        # 语言设置部分
        language_group = QGroupBox("语言设置")
        language_layout = QVBoxLayout(language_group)
        
        language_select_layout = QHBoxLayout()
        language_select_layout.addWidget(QLabel("识别语言:"))
        self.language_selector = QComboBox()
        self.language_selector.addItems(["中文", "英文", "日语", "法语", "德语"])
        language_select_layout.addWidget(self.language_selector)
        
        language_layout.addLayout(language_select_layout)
        
        # 音频设置部分
        audio_group = QGroupBox("音频设置")
        audio_layout = QVBoxLayout(audio_group)
        
        # 发送频率设置
        send_interval_layout = QHBoxLayout()
        send_interval_layout.addWidget(QLabel("自动发送频率(毫秒):"))
        self.send_interval = QSpinBox()
        self.send_interval.setRange(100, 5000)
        self.send_interval.setValue(500)
        self.send_interval.setSingleStep(100)
        send_interval_layout.addWidget(self.send_interval)
        self.auto_send_checkbox = QCheckBox("自动发送")
        self.auto_send_checkbox.setChecked(True)
        send_interval_layout.addWidget(self.auto_send_checkbox)
        
        mic_layout = QHBoxLayout()
        mic_layout.addWidget(QLabel("麦克风:"))
        self.mic_selector = QComboBox()
        mic_layout.addWidget(self.mic_selector)
        
        speaker_layout = QHBoxLayout()
        speaker_layout.addWidget(QLabel("扬声器:"))
        self.speaker_selector = QComboBox()
        speaker_layout.addWidget(self.speaker_selector)
        
        audio_buttons_layout = QHBoxLayout()
        self.start_record_button = QPushButton("开始录音")
        self.stop_record_button = QPushButton("停止录音")
        self.stop_record_button.setEnabled(False)
        audio_buttons_layout.addWidget(self.start_record_button)
        audio_buttons_layout.addWidget(self.stop_record_button)
        
        audio_layout.addLayout(send_interval_layout)
        audio_layout.addLayout(mic_layout)
        audio_layout.addLayout(speaker_layout)
        audio_layout.addLayout(audio_buttons_layout)
        
        # 音频频谱图部分
        visualizer_group = QGroupBox("音频频谱图")
        visualizer_layout = QVBoxLayout(visualizer_group)
        
        self.visualizer = AudioVisualizer()
        
        visualizer_layout.addWidget(self.visualizer)
        
        # 识别结果部分
        result_group = QGroupBox("识别结果")
        result_layout = QVBoxLayout(result_group)
        
        # self.result_text = QTextEdit()
        # self.result_text.setReadOnly(True)

        self.result = QLabel("")
        result_layout.addWidget(self.result)
        
        self.list_text = QListWidget()
        
        result_layout.addWidget(self.list_text)
        # result_layout.addWidget(self.result_text)
        
        
        # 添加所有部件到主布局
        main_layout.addWidget(server_group)
        main_layout.addWidget(language_group)
        main_layout.addWidget(audio_group)
        main_layout.addWidget(visualizer_group)
        main_layout.addWidget(result_group)
        
        self.setCentralWidget(central_widget)
    
    def _connect_signals(self):
        # 连接WebSocket信号
        self.ws_client.message_received.connect(self.on_message_received)
        self.ws_client.connection_status.connect(self.on_connection_status)
        
        # 连接按钮信号
        self.connect_button.clicked.connect(self.on_connect)
        self.disconnect_button.clicked.connect(self.on_disconnect)
        self.start_record_button.clicked.connect(self.on_start_recording)
        self.stop_record_button.clicked.connect(self.on_stop_recording)
    
    def _load_audio_devices(self):
        input_devices, output_devices = self.audio_handler.get_devices()
        
        # 添加麦克风设备
        for device_id, device_name in input_devices:
            self.mic_selector.addItem(device_name, device_id)
        
        # 添加扬声器设备
        for device_id, device_name in output_devices:
            self.speaker_selector.addItem(device_name, device_id)
    
    def update_visualizer(self, data):
        self.visualizer.update_data(data)
    
    def on_connect(self):
        host = self.server_host.text()
        port = self.server_port.value()
        
        self.connect_button.setEnabled(False)
        self.connection_status.setText("正在连接...")
        
        # 启动连接
        self.ws_client.connect_to_server(host, port)
    
    def on_disconnect(self):
        self.ws_client.disconnect()
        self.on_stop_recording()
    
    def on_connection_status(self, connected, message):
        self.connection_status.setText(message)
        
        if connected:
            self.connect_button.setEnabled(False)
            self.disconnect_button.setEnabled(True)
            self.start_record_button.setEnabled(True)
        else:
            self.connect_button.setEnabled(True)
            self.disconnect_button.setEnabled(False)
            self.start_record_button.setEnabled(False)
            self.stop_record_button.setEnabled(False)
    
    def on_start_recording(self):
        device_id = self.mic_selector.currentData()
        if device_id is not None:
            self.audio_handler.start_recording(device_id)
            self.start_record_button.setEnabled(False)
            self.stop_record_button.setEnabled(True)
            
            # 如果勾选了自动发送，启动自动发送线程
            if self.auto_send_checkbox.isChecked():
                interval = self.send_interval.value()
                self.audio_handler.start_auto_send(interval, self.auto_send_audio)
    
    def on_stop_recording(self):
        self.audio_handler.stop_recording()
        self.audio_handler.stop_auto_send()
        self.start_record_button.setEnabled(True)
        self.stop_record_button.setEnabled(False)
    
    def on_recognize(self):
        # 获取音频数据
        audio_data = self.audio_handler.get_audio_data()
        
        if len(audio_data) == 0:
            self.connection_status.setText("没有可用的音频数据")
            return
        
        # 获取当前选择的语言
        lang_map = {"中文": "zh", "英文": "en", "日语": "ja", "法语": "fr", "德语": "de"}
        selected_lang = lang_map[self.language_selector.currentText()]
        
        # 准备数据
        data_to_send = {
            "type": "audio_data",
            "language": selected_lang,
            "data": audio_data.tolist()
        }
        
        # 发送数据
        success = self.ws_client.send_data(data_to_send)
        if success:
            self.connection_status.setText("已发送音频数据，等待识别结果...")
        else:
            self.connection_status.setText("发送音频数据失败")
    
    def on_message_received(self, message):
        try:
            # 尝试解析JSON
            data = json.loads(message)
            if isinstance(data, dict) and "data" in data:
                # self.result_text.setText(data["data"])
                if data["data"].startswith("L:"):
                    self.result.setText(data["data"][2:])
                elif data["data"].startswith("T:"):
                    # 将翻译结果添加到列表顶部
                    self.list_text.insertItem(0, data["data"][2:])
                    # self.list_text.setText(data["data"][2:])
            else:
                # self.result_text.setText("错误: " + str(data))
                pass
        except:
            # 如果不是JSON，直接显示文本
            # self.result_text.setText("JSON错误: " + message)
            pass
    
    def auto_send_audio(self, audio_data):
        """自动发送音频数据的回调函数"""
        # 获取当前选择的语言
        lang_map = {"中文": "zh", "英文": "en", "日语": "ja", "法语": "fr", "德语": "de"}
        selected_lang = lang_map[self.language_selector.currentText()]
        
        # 计算应该发送的数据量（基于时间间隔）
        interval_ms = self.send_interval.value()
        sample_rate = 16000  # 采样率为16kHz
        samples_to_send = int((interval_ms / 1000.0) * sample_rate)
        
        # 如果有足够的数据，只发送最近的数据
        if len(audio_data) > samples_to_send:
            audio_data = audio_data[-samples_to_send:]
        
        # 准备数据
        data_to_send = {
            "type": "audio_data",
            "language": selected_lang,
            "data": audio_data.tolist()
        }
        
        # 发送数据
        self.ws_client.send_data(data_to_send)

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec_()) 