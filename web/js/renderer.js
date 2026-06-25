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
        const w = frame.displayWidth || frame.codedWidth;
        const h = frame.displayHeight || frame.codedHeight;
        if (this.canvas.width !== w || this.canvas.height !== h) {
          this.canvas.width = w;
          this.canvas.height = h;
        }
        try {
          this.ctx.drawImage(frame, 0, 0, w, h);
          this.drawnCount++;
          if (this.drawnCount === 1) {
            console.log('[renderer] first draw, frame format=' + frame.format +
                        ' coded=' + frame.codedWidth + 'x' + frame.codedHeight +
                        ' display=' + frame.displayWidth + 'x' + frame.displayHeight);
            // 取画布左上角像素，确认画上去的内容是不是全黑
            try {
              const px = this.ctx.getImageData(0, 0, 1, 1).data;
              console.log('[renderer] top-left pixel after draw:',
                          px[0], px[1], px[2], px[3]);
            } catch (e) {
              console.warn('[renderer] getImageData failed:', e.message);
            }
          }
        } catch (e) {
          console.error('[renderer] drawImage failed:', e.message);
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
