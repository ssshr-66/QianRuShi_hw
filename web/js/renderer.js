/*
 * renderer.js - 把解码出的 VideoFrame 画到 Canvas
 *
 * 低延迟策略：只保留"最新一帧"，用 requestAnimationFrame 驱动绘制，
 * 旧帧直接丢弃并 close()，避免积压导致延迟增长。
 */
export class Renderer {
  /** @param {HTMLCanvasElement} canvas */
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.latest = null;      // 最新待绘制的 VideoFrame
    this.running = false;
    this.drawnCount = 0;
  }

  start() {
    this.running = true;
    const loop = () => {
      if (!this.running) return;
      if (this.latest) {
        const frame = this.latest;
        this.latest = null;
        // 按视频实际尺寸调整 canvas 背景缓冲
        if (this.canvas.width !== frame.displayWidth ||
            this.canvas.height !== frame.displayHeight) {
          this.canvas.width = frame.displayWidth;
          this.canvas.height = frame.displayHeight;
        }
        try {
          this.ctx.drawImage(frame, 0, 0, this.canvas.width, this.canvas.height);
          this.drawnCount++;
        } finally {
          frame.close();   // 必须释放，否则 GPU 内存泄漏
        }
      }
      requestAnimationFrame(loop);
    };
    requestAnimationFrame(loop);
  }

  /** 收到一帧解码结果。替换最新帧（丢弃并关闭上一帧）。 */
  push(frame) {
    if (this.latest) {
      this.latest.close();   // 丢弃未画的旧帧
    }
    this.latest = frame;
  }

  stop() {
    this.running = false;
    if (this.latest) {
      this.latest.close();
      this.latest = null;
    }
  }
}
