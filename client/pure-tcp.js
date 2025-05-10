const net = require('net');

// 创建TCP客户端
console.log('正在连接到C++服务端...');

// 连接选项
const options = {
    host: '127.0.0.1',
    port: 3000
};

// 状态变量
let handshakeCompleted = false;
let messageInterval = null;

// 创建socket连接
const client = new net.Socket();

// 连接到服务器
client.connect(options, function() {
    console.log('已连接到服务器');
    
    // 首先尝试发送WebSocket握手请求
    console.log('发送WebSocket握手请求...');
    
    const handshakeRequest = 
        'GET / HTTP/1.1\r\n' +
        'Host: 127.0.0.1:3000\r\n' +
        'Upgrade: websocket\r\n' +
        'Connection: Upgrade\r\n' +
        'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n' + // 固定的测试密钥
        'Sec-WebSocket-Version: 13\r\n' +
        '\r\n';
    
    client.write(handshakeRequest);
});

// 处理接收到的数据
client.on('data', function(data) {
    if (!handshakeCompleted) {
        console.log('收到握手响应:');
        console.log(data.toString());
        
        // 检查握手是否成功
        if (data.toString().includes('HTTP/1.1 101')) {
            console.log('握手成功，开始发送测试消息...');
            handshakeCompleted = true;
            
            // 立即发送一条测试消息
            sendWebSocketMessage();
            
            // 设置定时器，每5秒发送一条测试消息
            messageInterval = setInterval(sendWebSocketMessage, 5000);
        } else {
            console.log('握手失败');
            client.destroy();
        }
    } else {
        // 已完成握手，处理接收到的WebSocket帧
        console.log('收到WebSocket帧:');
        
        try {
            // 解析WebSocket帧
            let offset = 0;
            
            // 帧头
            const firstByte = data[offset++];
            const secondByte = data[offset++];
            
            const fin = !!(firstByte & 0x80);
            const opcode = firstByte & 0x0F;
            const masked = !!(secondByte & 0x80);
            let payloadLength = secondByte & 0x7F;
            
            // 扩展长度处理
            if (payloadLength === 126) {
                payloadLength = data.readUInt16BE(offset);
                offset += 2;
            } else if (payloadLength === 127) {
                payloadLength = data.readUInt32BE(offset + 4); // 简化处理
                offset += 8;
            }
            
            // 掩码处理
            let mask;
            if (masked) {
                mask = data.slice(offset, offset + 4);
                offset += 4;
            }
            
            // 读取有效载荷
            const payload = data.slice(offset, offset + payloadLength);
            
            // 解码有效载荷
            let messageText;
            if (masked) {
                const unmaskedPayload = Buffer.alloc(payloadLength);
                for (let i = 0; i < payloadLength; i++) {
                    unmaskedPayload[i] = payload[i] ^ mask[i % 4];
                }
                messageText = unmaskedPayload.toString('utf8');
            } else {
                messageText = payload.toString('utf8');
            }
            
            console.log('OpCode:', opcode);
            console.log('载荷长度:', payloadLength);
            console.log('消息内容:', messageText);
            
            // 尝试解析JSON
            try {
                const jsonData = JSON.parse(messageText);
                console.log('JSON数据:', jsonData);
            } catch (e) {
                // 不是JSON
            }
            
        } catch (e) {
            // 帧解析错误，把原始数据输出
            console.error('解析WebSocket帧失败:', e.message);
            console.log('原始数据:', data.toString());
            console.log('十六进制:', data.toString('hex'));
        }
    }
});

// 发送WebSocket消息
function sendWebSocketMessage() {
    if (!handshakeCompleted) return;
    
    try {
        // 构造测试音频数据
        const audioData = [];
        for (let i = 0; i < 10; i++) {
            audioData.push(Math.sin(i / 10 * Math.PI) * 0.5);
        }
        
        const testMessage = JSON.stringify({
            type: "audio_data",
            data: audioData
        });
        
        console.log('发送测试消息:', testMessage);
        
        // 构造WebSocket帧
        const messageBuffer = Buffer.from(testMessage);
        const messageLength = messageBuffer.length;
        
        // 创建帧头
        let frameHeader;
        
        if (messageLength < 126) {
            frameHeader = Buffer.alloc(6); // 2字节帧头 + 4字节掩码
            frameHeader[0] = 0x81; // FIN + Text opcode
            frameHeader[1] = 0x80 | messageLength; // 掩码标志 + 长度
        } else if (messageLength < 65536) {
            frameHeader = Buffer.alloc(8); // 2字节帧头 + 2字节长度 + 4字节掩码
            frameHeader[0] = 0x81; // FIN + Text opcode
            frameHeader[1] = 0x80 | 126; // 掩码标志 + 126
            frameHeader.writeUInt16BE(messageLength, 2);
        } else {
            frameHeader = Buffer.alloc(14); // 2字节帧头 + 8字节长度 + 4字节掩码
            frameHeader[0] = 0x81; // FIN + Text opcode
            frameHeader[1] = 0x80 | 127; // 掩码标志 + 127
            frameHeader.writeUInt32BE(0, 2); // 高32位为0
            frameHeader.writeUInt32BE(messageLength, 6); // 低32位长度
        }
        
        // 设置掩码
        const maskOffset = frameHeader.length - 4;
        frameHeader[maskOffset] = 0x01;
        frameHeader[maskOffset + 1] = 0x02;
        frameHeader[maskOffset + 2] = 0x03;
        frameHeader[maskOffset + 3] = 0x04;
        
        // 应用掩码
        const maskedBuffer = Buffer.alloc(messageLength);
        for (let i = 0; i < messageLength; i++) {
            maskedBuffer[i] = messageBuffer[i] ^ frameHeader[maskOffset + (i % 4)];
        }
        
        // 发送WebSocket帧
        client.write(Buffer.concat([frameHeader, maskedBuffer]));
        
    } catch (e) {
        console.error('发送消息失败:', e.message);
    }
}

// 处理连接关闭
client.on('close', function() {
    console.log('连接已关闭');
    clearInterval(messageInterval);
});

// 处理错误
client.on('error', function(err) {
    console.error('连接错误:', err.message);
    clearInterval(messageInterval);
});

// 处理Ctrl+C
process.on('SIGINT', function() {
    console.log('正在关闭连接...');
    
    // 发送关闭帧
    if (handshakeCompleted) {
        const closeFrame = Buffer.from([0x88, 0x00]); // 关闭帧，无负载
        client.write(closeFrame);
    }
    
    // 关闭连接
    setTimeout(() => {
        client.destroy();
        process.exit(0);
    }, 500);
});

console.log('客户端已启动，等待连接...');
console.log('按Ctrl+C退出程序'); 