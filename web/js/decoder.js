/*
 * decoder.js - 用 WebCodecs VideoDecoder 解码 H.264 (annex-B)
 *
 * 关键点：
 *   - 服务端 libx264 输出 annex-B 码流（带 00 00 00 01 起始码，SPS/PPS 内嵌）
 *   - VideoDecoder 需要一个 codec 字符串，如 "avc1.42E01F"，
 *     由关键帧里的 SPS(profile_idc, constraint, level_idc) 推断
 *   - 必须从一个关键帧开始解码，之前的 P 帧丢弃
 *   - 解码出的 VideoFrame 交给 renderer，由其负责 close()
 */
export class H264Decoder {
  /** @param {(frame: VideoFrame) => void} onFrame */
  constructor(onFrame) {
    this.onFrame = onFrame;
    this.decoder = null;
    this.configured = false;
    this.gotKeyframe = false;
    this.errored = false;
  }

  static isSupported() {
    return typeof window !== 'undefined' && 'VideoDecoder' in window;
  }

  /** 在 annex-B 数据里查找第一个 SPS(NAL type 7)，返回其 [profile, constraint, level] */
  _findSpsCodec(data) {
    // 遍历起始码，定位 NAL 单元
    for (let i = 0; i + 4 < data.length; i++) {
      // 起始码 00 00 01 或 00 00 00 01
      let nalStart = -1;
      if (data[i] === 0 && data[i + 1] === 0 && data[i + 2] === 1) {
        nalStart = i + 3;
      } else if (data[i] === 0 && data[i + 1] === 0 &&
                 data[i + 2] === 0 && data[i + 3] === 1) {
        nalStart = i + 4;
      }
      if (nalStart < 0) continue;

      const nalType = data[nalStart] & 0x1f;
      if (nalType === 7) {
        // SPS：紧跟 NAL 头之后是 profile_idc, constraint_flags, level_idc
        const profile = data[nalStart + 1];
        const constraint = data[nalStart + 2];
        const level = data[nalStart + 3];
        const hex = (n) => n.toString(16).padStart(2, '0').toUpperCase();
        return `avc1.${hex(profile)}${hex(constraint)}${hex(level)}`;
      }
      i = nalStart; // 跳过已检查部分
    }
    return null;
  }

  _ensureConfigured(payload) {
    if (this.configured) return true;

    const codec = this._findSpsCodec(payload) || 'avc1.42E01F';
    this.decoder = new VideoDecoder({
      output: (frame) => this.onFrame(frame),
      error: (e) => {
        console.error('VideoDecoder error:', e.message);
        this.errored = true;
      },
    });
    this.decoder.configure({
      codec,
      optimizeForLatency: true,
    });
    this.configured = true;
    console.log('VideoDecoder configured with codec', codec);
    return true;
  }

  /**
   * 解码一帧。
   * @param {Uint8Array} payload  annex-B H.264 数据
   * @param {boolean} isKeyframe
   * @param {number} timestamp    毫秒
   */
  decode(payload, isKeyframe, timestamp) {
    if (this.errored) return;

    // 必须从关键帧开始
    if (!this.gotKeyframe) {
      if (!isKeyframe) return;     // 丢弃起播前的 P 帧
      this.gotKeyframe = true;
    }

    if (!this._ensureConfigured(payload)) return;
    if (this.decoder.state !== 'configured') return;

    // payload 是 Uint8Array 的视图，复制一份独立 buffer 给 chunk
    const data = payload.slice();
    const chunk = new EncodedVideoChunk({
      type: isKeyframe ? 'key' : 'delta',
      timestamp: timestamp * 1000, // 微秒
      data,
    });
    try {
      this.decoder.decode(chunk);
    } catch (e) {
      console.error('decode() threw:', e.message);
      this.errored = true;
    }
  }

  close() {
    if (this.decoder && this.decoder.state !== 'closed') {
      try { this.decoder.close(); } catch (_) {}
    }
    this.decoder = null;
    this.configured = false;
    this.gotKeyframe = false;
  }
}
