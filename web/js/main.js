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

function onMessage(buf) {
  // TODO(M5): parseFrame(buf) -> decoder -> renderer
  try {
    const frame = parseFrame(buf);
    // 占位：M5 会把 frame.payload 送入解码器
    void frame;
    placeholderEl.style.display = 'none';
  } catch (e) {
    console.warn('frame parse error:', e.message);
  }
}

// M0：页面能加载即说明服务端静态托管正常。
// 暂不自动连接 WebSocket（服务端 M3/M4 才提供 /stream）。
setStatus('connecting');
statusEl.textContent = 'M0：等待推流功能（M4/M5）';

// 预留：后续启用实时连接时取消注释
// const sock = new StreamSocket(onMessage, setStatus);
// sock.connect();

// 避免未使用告警（M0 占位）
void StreamSocket;
void onMessage;

console.log('Web client loaded (M0 skeleton). Static hosting works.');
