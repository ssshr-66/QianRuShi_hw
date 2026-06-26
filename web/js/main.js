/*
 * main.js - Web 客户端入口（M5：解码渲染）
 *
 * 数据流：socket(收二进制) -> protocol(解析帧) -> decoder(H.264解码) -> renderer(Canvas绘制)
 */
import { StreamSocket } from './socket.js';
import { parseFrame } from './protocol.js';
import { H264Decoder } from './decoder.js';
import { Renderer } from './renderer.js';

const statusEl = document.getElementById('status');
const statsEl = document.getElementById('stats');
const placeholderEl = document.getElementById('placeholder');
const canvas = document.getElementById('screen');

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

// WebCodecs 支持检测
if (!H264Decoder.isSupported()) {
  setStatus('disconnected');
  placeholderEl.textContent =
    '当前浏览器不支持 WebCodecs（VideoDecoder）。请用较新版本的 Chrome/Edge 打开。';
  throw new Error('WebCodecs not supported');
}

const renderer = new Renderer(canvas);
renderer.start();

const decoder = new H264Decoder((frame) => renderer.push(frame));

// 统计
let recvFrames = 0;
let recvBytes = 0;
let lastStatsT = performance.now();
let lastDrawn = 0;
let firstFrameShown = false;

// 端到端延迟统计（EMA 平滑）：最近一帧 = 收到时刻 - 帧捕获时间戳(ms)
let latencyEmaMs = 0;
let latencySamples = 0;

function updateStats() {
  const now = performance.now();
  const dt = (now - lastStatsT) / 1000;
  if (dt >= 1) {
    const fps = ((renderer.drawnCount - lastDrawn) / dt).toFixed(1);
    const lat = latencySamples > 0 ? latencyEmaMs.toFixed(0) : '--';
    statsEl.textContent =
      `${fps} fps | 延迟 ${lat} ms | 收包 ${recvFrames} | ${(recvBytes / 1024).toFixed(0)} KB`;
    lastStatsT = now;
    lastDrawn = renderer.drawnCount;
  }
  requestAnimationFrame(updateStats);
}
requestAnimationFrame(updateStats);

function onMessage(buf) {
  let frame;
  try {
    frame = parseFrame(buf);
  } catch (e) {
    console.warn('frame parse error:', e.message);
    return;
  }
  recvFrames++;
  recvBytes += buf.byteLength;

  // 端到端延迟 = 客户端当前时间(墙钟,ms) - 帧捕获时间戳(墙钟,ms)
  // 要求 Mac 与虚拟机时钟差不多对齐(NTP)；偶尔会有负数(时钟差)，按 0 处理。
  const nowMs = Date.now();
  const latency = nowMs - frame.timestamp;
  if (recvFrames <= 3) {
    console.log('[latency] nowMs=' + nowMs + ' frame.ts=' + frame.timestamp +
                ' diff=' + latency + 'ms');
  }
  if (latency >= 0 && latency < 60000) {  // 过滤异常值
    const alpha = 0.2; // EMA 平滑因子
    latencyEmaMs = latencySamples === 0 ? latency
                                        : alpha * latency + (1 - alpha) * latencyEmaMs;
    latencySamples++;
  }

  decoder.decode(frame.payload, frame.isKeyframe, frame.timestamp);

  if (!firstFrameShown) {
    firstFrameShown = true;
    placeholderEl.style.display = 'none'; // 有画面后隐藏占位文字
  }
}

const sock = new StreamSocket(onMessage, setStatus);
sock.connect();

console.log('Web client loaded (M5). Decoding + rendering enabled.');
