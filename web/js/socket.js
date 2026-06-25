/*
 * socket.js - WebSocket 连接管理（重连 + 二进制接收）
 *
 * 当前状态：M5 完整实现连接/重连；M0 仅提供骨架，便于后续接入。
 */
const WS_PATH = `ws://${location.host}/stream`;
const MAX_RECONNECT_MS = 5000;

export class StreamSocket {
  /**
   * @param {(buf: ArrayBuffer) => void} onMessage 收到二进制帧回调
   * @param {(state: string) => void} onState 状态变化回调
   */
  constructor(onMessage, onState) {
    this.onMessage = onMessage;
    this.onState = onState;
    this.ws = null;
    this.reconnectMs = 250;
    this.shouldRun = false;
  }

  connect() {
    this.shouldRun = true;
    this._open();
  }

  _open() {
    this.onState('connecting');
    const ws = new WebSocket(WS_PATH);
    ws.binaryType = 'arraybuffer'; // 必须：接收二进制帧
    this.ws = ws;

    ws.onopen = () => {
      this.reconnectMs = 250;
      this.onState('live');
      // TODO(M5): 连接后请求关键帧以快速起播
    };
    ws.onmessage = (ev) => this.onMessage(ev.data);
    ws.onclose = () => {
      this.onState('reconnecting');
      if (this.shouldRun) this._scheduleReconnect();
    };
    ws.onerror = () => ws.close();
  }

  _scheduleReconnect() {
    setTimeout(() => {
      if (this.shouldRun) this._open();
    }, this.reconnectMs);
    this.reconnectMs = Math.min(this.reconnectMs * 2, MAX_RECONNECT_MS);
  }

  close() {
    this.shouldRun = false;
    if (this.ws) this.ws.close();
  }
}
