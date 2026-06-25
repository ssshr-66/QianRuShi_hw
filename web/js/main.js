/*
 * main.js - Web 客户端入口（M0 骨架）
 *
 * 当前状态：仅做 UI 状态展示。M5 会接入 socket -> protocol -> decoder -> renderer。
 */
import { StreamSocket } from './socket.js';
import { parseFrame } from './protocol.js';

const statusEl = document.getElementById('status');
const placeholderEl = document.getElementById('placeholder');

function setStatus(state) {
  const labels = {
    connecting: '连接中',
    live: '直播中',
    reconnecting: '重连中',
    disconnected: '已断开',
  };
  statusEl.textContent = labels[state] || state;
  statusEl.className = 'status status--' + state;
}

let frameCount = 0;

function onMessage(buf) {
  // M3：能收到消息说明 WebSocket 推流通；M5 会真正解码渲染
  frameCount++;
  try {
    const frame = parseFrame(buf);
    placeholderEl.textContent =
      `M3：已收到 ${frameCount} 个数据帧（${frame.payload.length} bytes，` +
      `${frame.isKeyframe ? '关键帧' : 'P帧'}）。M5 将解码渲染。`;
  } catch (e) {
    // M3 阶段服务端可能还没按帧协议发送，收到任何二进制都算连接成功
    placeholderEl.textContent = `M3：WebSocket 已收到 ${buf.byteLength} bytes 数据。`;
  }
}

// M3：连接 WebSocket，验证握手与连接（服务端 M4 才会真正推流）
const sock = new StreamSocket(onMessage, setStatus);
sock.connect();

console.log('Web client loaded (M3). Connecting WebSocket...');
