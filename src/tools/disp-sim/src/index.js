#!/usr/bin/env node

import { SerialPort } from 'serialport';
import {
  FrameDecoder,
  MessageType,
  encodeFrame,
  helloPayload,
  numberValuePayload,
  parseSubscription,
  textValuePayload,
} from './protocol.js';

function usage() {
  console.log(`Usage:
  npm run ports
  npm start
  node src/index.js --port COM7 [--baud 115200] [--interval 500] [--verbose]

With no arguments, npm start automatically selects an ST-Link virtual COM port.
The simulator then waits for the display's subscription snapshot and generates
values for the requested Signal K-style paths.`);
}

function parseArguments(argv) {
  const options = { baudRate: 115200, intervalMs: 500, verbose: false, list: false };
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    const take = () => {
      index += 1;
      if (index >= argv.length) throw new Error(`missing value for ${argument}`);
      return argv[index];
    };
    if (argument === '--port') options.path = take();
    else if (argument.startsWith('--port=')) options.path = argument.slice('--port='.length);
    else if (argument === '--baud') options.baudRate = Number.parseInt(take(), 10);
    else if (argument.startsWith('--baud=')) options.baudRate = Number.parseInt(argument.slice('--baud='.length), 10);
    else if (argument === '--interval') options.intervalMs = Number.parseInt(take(), 10);
    else if (argument.startsWith('--interval=')) options.intervalMs = Number.parseInt(argument.slice('--interval='.length), 10);
    else if (argument === '--verbose') options.verbose = true;
    else if (argument === '--list') options.list = true;
    else if (argument === '--help' || argument === '-h') options.help = true;
    else if (!argument.startsWith('-') && !options.path) options.path = argument;
    else throw new Error(`unknown argument: ${argument}`);
  }
  if (!Number.isInteger(options.baudRate) || options.baudRate <= 0) throw new Error('invalid baud rate');
  if (!Number.isInteger(options.intervalMs) || options.intervalMs < 50) {
    throw new Error('interval must be at least 50 ms');
  }
  return options;
}

function describeHello(payload) {
  if (payload.length < 2 || payload.length !== 2 + payload[1]) return 'malformed peer';
  return `${payload.subarray(2).toString('utf8')} (role ${payload[0]})`;
}

function syntheticValue(path, elapsedSeconds) {
  if (/datetime|dateTime|timestamp|time$/i.test(path)) return new Date().toISOString();
  if (/speedOverGround/i.test(path)) return 3.1 + 0.45 * Math.sin(elapsedSeconds / 4);
  if (/courseOverGround/i.test(path)) return (1.9 + elapsedSeconds * 0.025) % (Math.PI * 2);
  if (/depth/i.test(path)) return 8.4 + 0.6 * Math.sin(elapsedSeconds / 7);
  if (/temperature/i.test(path)) return 288.15 + 1.5 * Math.sin(elapsedSeconds / 20);
  if (/voltage/i.test(path)) return 12.6 + 0.12 * Math.sin(elapsedSeconds / 9);
  return 10 + 2 * Math.sin(elapsedSeconds / 5);
}

async function listPorts() {
  const ports = await SerialPort.list();
  if (ports.length === 0) {
    console.log('No serial ports found.');
    return;
  }
  for (const port of ports) {
    console.log([port.path, port.manufacturer, port.friendlyName].filter(Boolean).join(' | '));
  }
}

async function detectPort() {
  const ports = await SerialPort.list();
  const stLinks = ports.filter(port =>
    /st-?link/i.test(`${port.manufacturer ?? ''} ${port.friendlyName ?? ''}`));
  if (stLinks.length === 1) {
    console.log(`Auto-detected ST-Link virtual COM port: ${stLinks[0].path}`);
    return stLinks[0].path;
  }
  if (stLinks.length > 1) {
    throw new Error(`multiple ST-Link ports found: ${stLinks.map(port => port.path).join(', ')}`);
  }
  if (ports.length === 1) {
    console.log(`Auto-selected the only serial port: ${ports[0].path}`);
    return ports[0].path;
  }
  const available = ports.map(port => port.path).join(', ') || 'none';
  throw new Error(`could not uniquely select a serial port; available ports: ${available}`);
}

function run(options) {
  const port = new SerialPort({ path: options.path, baudRate: options.baudRate });
  const decoder = new FrameDecoder();
  const startedAt = Date.now();
  const active = new Map();
  let pending = null;
  let pendingSection = 0xffff;
  let expectedSubscriptions = 0;
  let sequence = 0;
  let receivedBytes = 0;
  let receivedFrames = 0;
  let subscriptionSnapshotReceived = false;
  let waitingNoticeShown = false;

  const send = (type, payload) => {
    const frame = encodeFrame(type, sequence, payload);
    sequence = (sequence + 1) & 0xff;
    port.write(frame);
    if (options.verbose) console.log(`TX type=0x${type.toString(16)} bytes=${frame.length}`);
  };

  port.on('open', () => {
    console.log(`Connected to ${options.path} at ${options.baudRate} baud.`);
    send(MessageType.HELLO, helloPayload(2, 'disp-sim'));
  });
  port.on('error', error => {
    console.error(`Serial error: ${error.message}`);
    clearInterval(valueTimer);
    clearInterval(pingTimer);
    clearInterval(helloTimer);
    clearInterval(statusTimer);
    process.exitCode = 1;
  });
  port.on('close', () => {
    clearInterval(valueTimer);
    clearInterval(pingTimer);
    clearInterval(helloTimer);
    clearInterval(statusTimer);
    console.log('Serial port closed.');
  });
  port.on('data', chunk => {
    receivedBytes += chunk.length;
    if (options.verbose) console.log(`RX bytes=${chunk.length} total=${receivedBytes}`);
    for (const message of decoder.push(chunk)) {
      receivedFrames += 1;
      if (options.verbose) {
        console.log(`RX type=0x${message.type.toString(16)} seq=${message.sequence} payload=${message.payload.length}`);
      }
      try {
        if (message.type === MessageType.HELLO) {
          console.log(`Display hello: ${describeHello(message.payload)}`);
        } else if (message.type === MessageType.SUBSCRIPTIONS_BEGIN && message.payload.length === 4) {
          pendingSection = message.payload.readUInt16LE(0);
          expectedSubscriptions = message.payload.readUInt16LE(2);
          pending = new Map();
        } else if (message.type === MessageType.SUBSCRIBE && pending) {
          const subscription = parseSubscription(message.payload);
          subscription.nextAt = Date.now();
          pending.set(subscription.sourceIndex, subscription);
        } else if (message.type === MessageType.SUBSCRIPTIONS_END && pending) {
          active.clear();
          for (const [index, subscription] of pending) active.set(index, subscription);
          const suffix = active.size === expectedSubscriptions ? ''
            : ` (expected ${expectedSubscriptions})`;
          console.log(`Section ${pendingSection}: ${active.size} active subscription(s)${suffix}`);
          for (const source of active.values()) {
            console.log(`  [${source.sourceIndex}] ${source.provider}:${source.path}` +
              `${source.permanent ? ' permanent' : ''}`);
          }
          pending = null;
          subscriptionSnapshotReceived = true;
          waitingNoticeShown = false;
        } else if (message.type === MessageType.PING && message.payload.length === 4) {
          send(MessageType.PONG, message.payload);
        } else if (message.type === MessageType.PONG && options.verbose) {
          console.log('Pong received.');
        }
      } catch (error) {
        console.warn(`Ignored malformed message: ${error.message}`);
      }
    }
  });

  const valueTimer = setInterval(() => {
    const now = Date.now();
    const elapsed = (now - startedAt) / 1000;
    for (const source of active.values()) {
      if (now < source.nextAt) continue;
      const period = source.periodMs > 0 ? Math.max(50, source.periodMs) : options.intervalMs;
      source.nextAt = now + period;
      const value = syntheticValue(source.path, elapsed);
      if (typeof value === 'string') {
        send(MessageType.VALUE_TEXT, textValuePayload(source.sourceIndex, value));
      } else {
        send(MessageType.VALUE_NUMBER, numberValuePayload(source.sourceIndex, value));
      }
    }
  }, 25);

  const pingTimer = setInterval(() => {
    const payload = Buffer.alloc(4);
    payload.writeUInt32LE((Date.now() - startedAt) >>> 0);
    send(MessageType.PING, payload);
  }, 5000);

  const helloTimer = setInterval(() => {
    if (!subscriptionSnapshotReceived) send(MessageType.HELLO, helloPayload(2, 'disp-sim'));
  }, 1000);

  const statusTimer = setInterval(() => {
    if (subscriptionSnapshotReceived || waitingNoticeShown) return;
    if (receivedBytes === 0) {
      console.log('Waiting for remote-a: no bytes received yet. The EEPROM OFFLINE modal does not block the link; press any button to dismiss it.');
    } else if (receivedFrames === 0) {
      console.log(`Received ${receivedBytes} byte(s), but no valid protocol frame (${decoder.errors} decode error(s)). Check baud and firmware version.`);
    } else {
      console.log('Display is responding, but has not sent a subscription snapshot yet.');
    }
    waitingNoticeShown = true;
  }, 2500);

  const shutdown = () => {
    clearInterval(valueTimer);
    clearInterval(pingTimer);
    clearInterval(helloTimer);
    clearInterval(statusTimer);
    if (port.isOpen) port.close(() => process.exit(0));
    else process.exit(process.exitCode ?? 0);
  };
  process.on('SIGINT', shutdown);
  process.on('SIGTERM', shutdown);
}

try {
  const options = parseArguments(process.argv.slice(2));
  if (options.help) usage();
  else if (options.list) await listPorts();
  else {
    if (!options.path) options.path = await detectPort();
    run(options);
  }
} catch (error) {
  console.error(error.message);
  usage();
  process.exitCode = 1;
}
