/*
 * protocol.js - 二进制帧协议解析（必须与服务端 protocol/frame_proto.c 一致）
 *
 * 帧头 16 字节，大端：
 *   0   1  type        (0=video, 1=keyframe, 2=control)
 *   1   1  flags
 *   2   2  reserved
 *   4   8  timestamp   (ms)
 *   12  4  payload_len
 *   16  N  payload
 */
export const HEADER_SIZE = 16;

export const PROTO_TYPE = {
  VIDEO: 0,
  KEYFRAME: 1,
  CONTROL: 2,
};

/**
 * 解析一个 ArrayBuffer 为帧对象。
 * @param {ArrayBuffer} buf
 * @returns {{type:number, flags:number, timestamp:number, payload:Uint8Array, isKeyframe:boolean}}
 */
export function parseFrame(buf) {
  if (buf.byteLength < HEADER_SIZE) {
    throw new Error('short frame: ' + buf.byteLength);
  }
  const dv = new DataView(buf);
  const type = dv.getUint8(0);
  const flags = dv.getUint8(1);
  // 大端 64 位时间戳；用 Number 足够（毫秒不会超出安全整数范围）
  const timestamp = Number(dv.getBigUint64(4, false));
  const payloadLen = dv.getUint32(12, false);

  if (HEADER_SIZE + payloadLen > buf.byteLength) {
    throw new Error('truncated payload');
  }
  const payload = new Uint8Array(buf, HEADER_SIZE, payloadLen);

  return {
    type,
    flags,
    timestamp,
    payload,
    isKeyframe: type === PROTO_TYPE.KEYFRAME,
  };
}
