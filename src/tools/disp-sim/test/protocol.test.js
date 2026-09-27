import test from 'node:test';
import assert from 'node:assert/strict';
import {
  FrameDecoder,
  MessageType,
  alertIdPayload,
  alertUpdatePayload,
  crc16Ccitt,
  crc32,
  encodeFrame,
  numberValuePayload,
  parseAlertAction,
} from '../src/protocol.js';

test('CRC-16/CCITT-FALSE matches the standard check value', () => {
  assert.equal(crc16Ccitt(Buffer.from('123456789')), 0x29b1);
});

test('alert payloads preserve identity, severity, occurrence, and text', () => {
  const payload = alertUpdatePayload(3, 17, 'co.aft', 'CO ALARM', 'Leave now');
  assert.equal(payload[0], 3);
  assert.equal(payload.readUInt32LE(2), 17);
  assert.equal(payload.subarray(9, 15).toString(), 'co.aft');
  assert.deepEqual(alertIdPayload('co.aft'), Buffer.from([6, 99, 111, 46, 97, 102, 116]));

  const action = Buffer.alloc(12);
  action[0] = 1;
  action.writeUInt32LE(17, 1);
  action[5] = 6;
  action.write('co.aft', 6);
  assert.deepEqual(parseAlertAction(action), { action: 1, occurrence: 17, id: 'co.aft' });
});

test('CRC-32 matches the standard check value', () => {
  assert.equal(crc32(Buffer.from('123456789')), 0xcbf43926);
});

test('frames round-trip through arbitrary chunks', () => {
  const payload = Buffer.from([0, 1, 2, 0, 3, 4, 0]);
  const encoded = encodeFrame(MessageType.SUBSCRIBE, 42, payload, 0x5a);
  const decoder = new FrameDecoder();
  assert.deepEqual(decoder.push(encoded.subarray(0, 3)), []);
  assert.deepEqual(decoder.push(encoded.subarray(3, 7)), []);
  const messages = decoder.push(encoded.subarray(7));
  assert.equal(messages.length, 1);
  assert.equal(messages[0].type, MessageType.SUBSCRIBE);
  assert.equal(messages[0].sequence, 42);
  assert.equal(messages[0].flags, 0x5a);
  assert.deepEqual(messages[0].payload, payload);
});

test('corrupt frames are rejected and the next frame is decoded', () => {
  const bad = encodeFrame(MessageType.VALUE_NUMBER, 1, numberValuePayload(2, 12.5));
  bad[3] ^= 0x20;
  const good = encodeFrame(MessageType.VALUE_NUMBER, 2, numberValuePayload(3, 4.5));
  const decoder = new FrameDecoder();
  const messages = decoder.push(Buffer.concat([bad, good]));
  assert.equal(decoder.errors, 1);
  assert.equal(messages.length, 1);
  assert.equal(messages[0].sequence, 2);
});
