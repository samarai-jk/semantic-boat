'use strict';

// Screenshots of remote-a displays over the addressed RS485 bus
// (src/lib/semantic-link/PROTOCOL.md, "Screenshot"): the framebuffer is read
// piece by piece while the display session waits, and returned as a PNG in
// the orientation the display shows.

const zlib = require('node:zlib');

class ScreenCapture {
  constructor({ master, address, protocol }) {
    this.master = master; this.address = address; this.protocol = protocol;
  }

  // One piece; older queued messages of the display go to its session.
  async piece(offset) {
    const { MessageType, parseScreenData } = this.protocol;
    const payload = Buffer.alloc(4); payload.writeUInt32LE(offset, 0);
    let reply = await this.master.exchange(this.address, MessageType.SCREEN_REQUEST, payload, MessageType.SCREEN_DATA);
    for (let tries = 0; tries < 8; tries += 1) {
      if (reply.type === MessageType.SCREEN_DATA) {
        const screen = parseScreenData(reply.payload);
        if (screen.offset === offset) return screen;
      }
      reply = await this.master.exchange(this.address, MessageType.BUS_POLL, Buffer.alloc(0), MessageType.SCREEN_DATA);
    }
    throw new Error('The display does not answer screenshot requests (firmware too old?)');
  }

  async capture() {
    this.master.pause(this.address, true);
    let frame; let width; let height; let rotation;
    try {
      for (let offset = 0; !frame || offset < frame.length;) {
        const screen = await this.piece(offset);
        if (!screen.width || !screen.height) throw new Error('The display is busy (installing a configuration)');
        if (!frame) {
          ({ width, height, rotation } = screen);
          frame = Buffer.alloc(((width + 7) >> 3) * height);
        }
        if (!screen.data.length) throw new Error('The display sent no screen data');
        screen.data.copy(frame, offset);
        offset += screen.data.length;
      }
    } finally {
      this.master.pause(this.address, false);
    }
    return encodePng(frame, width, height, rotation, this.protocol.crc32);
  }
}

// The panel's framebuffer (rows of width bits, most significant bit first,
// 1 white) turned by the display's rotation (MonochromeCanvas), as a 1-bit
// grayscale PNG, in which 1 is white as well.
function encodePng(frame, width, height, rotation, crc32) {
  const turned = rotation % 2 === 1;
  const outWidth = turned ? height : width; const outHeight = turned ? width : height;
  const raw = (x, y) => (frame[y * ((width + 7) >> 3) + (x >> 3)] >> (7 - (x & 7))) & 1;
  const pixel = [(x, y) => raw(x, y), (x, y) => raw(y, height - x - 1),
    (x, y) => raw(width - x - 1, height - y - 1), (x, y) => raw(width - y - 1, x)][rotation & 3];
  const stride = Math.ceil(outWidth / 8) + 1;
  const rows = Buffer.alloc(stride * outHeight);
  for (let y = 0; y < outHeight; y += 1) {
    for (let x = 0; x < outWidth; x += 1) if (pixel(x, y)) rows[y * stride + 1 + (x >> 3)] |= 0x80 >> (x & 7);
  }
  const chunk = (type, data) => {
    const body = Buffer.concat([Buffer.from(type, 'ascii'), data]);
    const length = Buffer.alloc(4); length.writeUInt32BE(data.length, 0);
    const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(body), 0);
    return Buffer.concat([length, body, crc]);
  };
  const header = Buffer.alloc(13);
  header.writeUInt32BE(outWidth, 0); header.writeUInt32BE(outHeight, 4); header[8] = 1; // 1 bit grayscale
  return Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk('IHDR', header),
    chunk('IDAT', zlib.deflateSync(rows)), chunk('IEND', Buffer.alloc(0))]);
}

module.exports = { ScreenCapture, encodePng };
