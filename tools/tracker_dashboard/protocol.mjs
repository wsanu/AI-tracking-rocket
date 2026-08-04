const HUIYAN_HEAD_0 = 0x78;
const HUIYAN_HEAD_1 = 0x07;
const HUIYAN_END = 0x79;
const PERIODIC_CMD = 0x00;
const MISS_DISTANCE_CMD = 0x81;
const MISS_DISTANCE_LENGTH = 14;

const MAVLINK_V1_MAGIC = 0xfe;
const MAVLINK_V2_MAGIC = 0xfd;
const MAVLINK_DEBUG_FLOAT_ARRAY_ID = 350;
const TRACKER_DEBUG_ARRAY_ID = 0x8100;
const TRACKER_DEBUG_ARRAY_NAME = "TRK_V31";

export function checksum8(bytes) {
  let sum = 0;

  for (const byte of bytes) {
    sum = (sum + byte) & 0xff;
  }

  return sum;
}

export function bytesToHex(bytes) {
  return Array.from(bytes, (byte) => byte.toString(16).padStart(2, "0").toUpperCase()).join(" ");
}

export class HuiyanStreamParser {
  constructor({ onFrame = () => {}, onError = () => {} } = {}) {
    this.onFrame = onFrame;
    this.onError = onError;
    this.buffer = [];
  }

  reset() {
    this.buffer = [];
  }

  push(chunk) {
    this.buffer.push(...chunk);

    while (this.buffer.length >= 2) {
      const start = this.findHeader();

      if (start < 0) {
        this.buffer = this.buffer.at(-1) === HUIYAN_HEAD_0 ? [HUIYAN_HEAD_0] : [];
        return;
      }

      if (start > 0) {
        this.buffer.splice(0, start);
      }

      if (this.buffer.length < 5) {
        return;
      }

      const payloadLength = this.buffer[4];
      const frameLength = payloadLength + 7;

      if (this.buffer.length < frameLength) {
        return;
      }

      const raw = Uint8Array.from(this.buffer.slice(0, frameLength));

      if (raw[frameLength - 1] !== HUIYAN_END) {
        this.onError({ type: "footer", raw });
        this.buffer.shift();
        continue;
      }

      const checksumIndex = frameLength - 2;
      const expected = checksum8(raw.slice(2, checksumIndex));

      if (raw[checksumIndex] !== expected) {
        this.onError({ type: "checksum", raw, expected, actual: raw[checksumIndex] });
        this.buffer.splice(0, frameLength);
        continue;
      }

      const frame = {
        transport: "huiyan",
        cmd0: raw[2],
        cmd1: raw[3],
        payload: raw.slice(5, checksumIndex),
        raw,
      };

      this.buffer.splice(0, frameLength);
      this.onFrame(frame);
    }
  }

  findHeader() {
    for (let index = 0; index < this.buffer.length - 1; index += 1) {
      if (this.buffer[index] === HUIYAN_HEAD_0 && this.buffer[index + 1] === HUIYAN_HEAD_1) {
        return index;
      }
    }

    return -1;
  }
}

export function parseMissDistanceFrame(frame, imageWidth = 1280, imageHeight = 720) {
  if (
    frame.cmd0 !== PERIODIC_CMD ||
    frame.cmd1 !== MISS_DISTANCE_CMD ||
    frame.payload.length !== MISS_DISTANCE_LENGTH
  ) {
    return null;
  }

  const payload = frame.payload;
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  const status = payload[0];
  const angleMode = (status & 0x04) !== 0;
  const running = (status & 0x02) === 0;
  const dataValid = (status & 0x01) !== 0;
  const offsetX = angleMode ? view.getFloat32(2, true) : view.getInt32(2, true);
  const offsetY = angleMode ? view.getFloat32(6, true) : view.getInt32(6, true);

  return {
    transport: frame.transport,
    valid: dataValid && running,
    running,
    angleMode,
    targetId: payload[1],
    offsetX,
    offsetY,
    imageX: angleMode ? imageWidth / 2 : imageWidth / 2 + offsetX,
    imageY: angleMode ? imageHeight / 2 : imageHeight / 2 - offsetY,
    boxWidth: view.getUint16(10, true),
    boxHeight: view.getUint16(12, true),
    bearingXRad: angleMode ? (offsetX * Math.PI) / 180 : null,
    bearingYRad: angleMode ? (offsetY * Math.PI) / 180 : null,
    raw: frame.raw,
  };
}

export function buildMissDistanceFrame({
  valid = true,
  running = true,
  angleMode = false,
  targetId = 0,
  offsetX = 0,
  offsetY = 0,
  boxWidth = 120,
  boxHeight = 80,
} = {}) {
  const payload = new Uint8Array(MISS_DISTANCE_LENGTH);
  const view = new DataView(payload.buffer);
  payload[0] = (angleMode ? 0x04 : 0) | (running ? 0 : 0x02) | (valid ? 0x01 : 0);
  payload[1] = targetId & 0xff;

  if (angleMode) {
    view.setFloat32(2, offsetX, true);
    view.setFloat32(6, offsetY, true);
  } else {
    view.setInt32(2, Math.round(offsetX), true);
    view.setInt32(6, Math.round(offsetY), true);
  }

  view.setUint16(10, Math.max(0, Math.min(0xffff, Math.round(boxWidth))), true);
  view.setUint16(12, Math.max(0, Math.min(0xffff, Math.round(boxHeight))), true);

  const frame = new Uint8Array(payload.length + 7);
  frame.set([HUIYAN_HEAD_0, HUIYAN_HEAD_1, PERIODIC_CMD, MISS_DISTANCE_CMD, payload.length], 0);
  frame.set(payload, 5);
  frame[frame.length - 2] = checksum8(frame.slice(2, frame.length - 2));
  frame[frame.length - 1] = HUIYAN_END;
  return frame;
}

export class MavlinkStreamParser {
  constructor({ onTracker = () => {}, onFrame = () => {}, onError = () => {} } = {}) {
    this.onTracker = onTracker;
    this.onFrame = onFrame;
    this.onError = onError;
    this.buffer = [];
  }

  reset() {
    this.buffer = [];
  }

  push(chunk) {
    this.buffer.push(...chunk);

    while (this.buffer.length > 0) {
      const start = this.buffer.findIndex((byte) => byte === MAVLINK_V1_MAGIC || byte === MAVLINK_V2_MAGIC);

      if (start < 0) {
        this.buffer = [];
        return;
      }

      if (start > 0) {
        this.buffer.splice(0, start);
      }

      const magic = this.buffer[0];
      const headerLength = magic === MAVLINK_V2_MAGIC ? 10 : 6;

      if (this.buffer.length < headerLength) {
        return;
      }

      const payloadLength = this.buffer[1];
      const signedLength = magic === MAVLINK_V2_MAGIC && (this.buffer[2] & 0x01) !== 0 ? 13 : 0;
      const frameLength = headerLength + payloadLength + 2 + signedLength;

      if (this.buffer.length < frameLength) {
        return;
      }

      const raw = Uint8Array.from(this.buffer.slice(0, frameLength));
      this.buffer.splice(0, frameLength);

      const messageId =
        magic === MAVLINK_V2_MAGIC
          ? raw[7] | (raw[8] << 8) | (raw[9] << 16)
          : raw[5];
      const payload = raw.slice(headerLength, headerLength + payloadLength);
      const frame = { transport: "mavlink", messageId, payload, raw };
      this.onFrame(frame);

      if (messageId === MAVLINK_DEBUG_FLOAT_ARRAY_ID) {
        const tracker = parseTrackerDebugArray(frame);

        if (tracker) {
          this.onTracker(tracker);
        }
      }
    }
  }
}

export function parseTrackerDebugArray(frame) {
  if (frame.messageId !== MAVLINK_DEBUG_FLOAT_ARRAY_ID || frame.payload.length < 68) {
    return null;
  }

  const payload = frame.payload;
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  const arrayId = view.getUint16(8, true);
  const name = String.fromCharCode(...payload.slice(10, 20)).replace(/\0.*$/, "");

  if (arrayId !== TRACKER_DEBUG_ARRAY_ID || name !== TRACKER_DEBUG_ARRAY_NAME) {
    return null;
  }

  const data = [];

  for (let offset = 20; offset + 4 <= payload.length; offset += 4) {
    data.push(view.getFloat32(offset, true));
  }

  if (data.length < 12) {
    return null;
  }

  return {
    transport: "mavlink",
    valid: data[0] > 0.5,
    running: data[1] > 0.5,
    angleMode: data[2] > 0.5,
    targetId: Math.round(data[3]),
    imageX: data[4],
    imageY: data[5],
    boxWidth: data[6],
    boxHeight: data[7],
    bearingXRad: data[8],
    bearingYRad: data[9],
    sizeRatio: data[10],
    frameCount: Math.round(data[11]),
    parseErrorCount: data.length > 12 ? Math.round(data[12]) : 0,
    raw: frame.raw,
  };
}

export const protocolConstants = Object.freeze({
  MAVLINK_DEBUG_FLOAT_ARRAY_ID,
  TRACKER_DEBUG_ARRAY_ID,
  TRACKER_DEBUG_ARRAY_NAME,
});
