#!/usr/bin/env node

import { readdir, readFile, mkdir } from 'node:fs/promises';
import path from 'node:path';
import readline from 'node:readline';
import { fileURLToPath } from 'node:url';
import { SerialPort } from 'serialport';
import {
  FrameDecoder,
  MessageType,
  alertIdPayload,
  alertUpdatePayload,
  crc32,
  encodeFrame,
  helloPayload,
  numberValuePayload,
  parseAlertAction,
  parseSubscription,
  textValuePayload,
} from './protocol.js';

const moduleDirectory = path.dirname(fileURLToPath(import.meta.url));
const defaultConfigDirectory = path.resolve(moduleDirectory, '..', 'configs');
const maximumConfigBytes = 8 * 1024;
const maximumConfigNameBytes = 47;
// Keep each encoded config-chunk frame within remote-a's 64-byte UART receive
// block. This also gives its interrupt receiver a clean boundary between
// frames instead of streaming maximum-size frames back-to-back.
const configChunkBytes = 32;
const configFrameGapMs = 20;
const snoozeDurationMs = 10_000;
const alertTemplates = Object.freeze({
  i: { level: 0, id: 'sim.system.update', title: 'SERVER MESSAGE',
       message: 'Signal K configuration was updated.' },
  w: { level: 1, id: 'sim.engine.temperature.warn', title: 'ENGINE TEMPERATURE',
       message: 'Engine coolant temperature is unusually high.' },
  a: { level: 2, id: 'sim.engine.overheat', title: 'ENGINE OVERHEATING',
       message: 'Stop the engine and investigate immediately.' },
  e: { level: 3, id: 'sim.co.aft-cabin', title: 'CO ALARM - AFT CABIN',
       message: 'Leave the cabin immediately and ventilate the vessel.' },
});

const delay = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));

function usage() {
  console.log(`Usage:
  npm run ports
  npm start
  node src/index.js --port COM7 [--baud 115200] [--interval 500]
                    [--config-dir ./configs] [--push-list] [--verbose]

With no arguments, npm start automatically selects an ST-Link virtual COM port.
The simulator then waits for the display's subscription snapshot and generates
values for the requested Signal K-style paths.`);
}

function parseArguments(argv) {
  const options = {
    baudRate: 115200,
    intervalMs: 500,
    configDirectory: defaultConfigDirectory,
    pushList: false,
    verbose: false,
    list: false,
  };
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
    else if (argument === '--config-dir') options.configDirectory = path.resolve(take());
    else if (argument.startsWith('--config-dir=')) options.configDirectory = path.resolve(argument.slice('--config-dir='.length));
    else if (argument === '--push-list') options.pushList = true;
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
  if (/datetime|timestamp|(?:^|\.)time$/i.test(path)) return new Date().toISOString();
  if (/firmwareVersion/i.test(path)) return 'dev-0.1';
  if (/linkStatus/i.test(path)) return 'ONLINE';
  if (/(?:^|\.)uptime$/i.test(path)) return 72 * 3600 + elapsedSeconds;
  if (/cpuLoad/i.test(path)) return 0.24 + 0.13 * Math.sin(elapsedSeconds / 11);
  if (/memoryUsed/i.test(path)) return 0.61 + 0.04 * Math.sin(elapsedSeconds / 17);
  if (/connectedClients/i.test(path)) return Math.round(8 + Math.sin(elapsedSeconds / 13));
  if (/linkLatency/i.test(path)) return 11 + 3 * Math.sin(elapsedSeconds / 3);
  if (/receivedValues/i.test(path)) return 18 + 5 * Math.sin(elapsedSeconds / 5);
  if (/speedOverGround/i.test(path)) return 3.1 + 0.45 * Math.sin(elapsedSeconds / 4);
  if (/speedThroughWater/i.test(path)) return 2.95 + 0.3 * Math.sin(elapsedSeconds / 5);
  if (/courseOverGround/i.test(path)) return (1.9 + elapsedSeconds * 0.025) % (Math.PI * 2);
  if (/headingTrue/i.test(path)) return (1.82 + elapsedSeconds * 0.02) % (Math.PI * 2);
  if (/bearingTrue/i.test(path)) return 2.28 + 0.04 * Math.sin(elapsedSeconds / 18);
  if (/distanceToWaypoint/i.test(path)) return Math.max(0.1, 7.6 - elapsedSeconds / 3600) * 1852;
  if (/estimatedTimeOfArrival/i.test(path)) {
    return new Date(Date.now() + 84 * 60 * 1000).toISOString().slice(11, 16);
  }
  if (/depth/i.test(path)) return 8.4 + 0.6 * Math.sin(elapsedSeconds / 7);
  if (/wind\.speedApparent/i.test(path)) return 7.2 + 1.1 * Math.sin(elapsedSeconds / 6);
  if (/wind\.angleApparent/i.test(path)) return 0.72 + 0.18 * Math.sin(elapsedSeconds / 9);
  if (/wind\.speedTrue/i.test(path)) return 6.1 + 0.8 * Math.sin(elapsedSeconds / 8);
  if (/wind\.directionTrue/i.test(path)) return 3.8 + 0.15 * Math.sin(elapsedSeconds / 14);
  if (/outside\.pressure/i.test(path)) return 101320 + 180 * Math.sin(elapsedSeconds / 40);
  if (/outside\.humidity/i.test(path)) return 0.72 + 0.05 * Math.sin(elapsedSeconds / 35);
  if (/revolutions|rpm/i.test(path)) return (1850 + 140 * Math.sin(elapsedSeconds / 3)) / 60;
  if (/oilPressure/i.test(path)) return 370000 + 15000 * Math.sin(elapsedSeconds / 7);
  if (/alternatorVoltage/i.test(path)) return 14.2 + 0.08 * Math.sin(elapsedSeconds / 8);
  if (/fuel\.rate/i.test(path)) return (5.8 + 0.6 * Math.sin(elapsedSeconds / 5)) / 3600000;
  if (/runTime/i.test(path)) return 438 * 3600 + elapsedSeconds;
  if (/coolantTemperature/i.test(path)) return 353.15 + 2 * Math.sin(elapsedSeconds / 25);
  if (/water\.temperature/i.test(path)) return 286.65 + 0.7 * Math.sin(elapsedSeconds / 30);
  if (/outside\.temperature/i.test(path)) return 290.15 + 1.5 * Math.sin(elapsedSeconds / 20);
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
  let unsolicitedListSent = false;
  let listId = 0;
  let transferId = 0;
  let transmitQueue = Promise.resolve();
  const activeAlerts = new Map();
  let nextTestAlertId = 1;

  const send = (type, payload) => {
    const frame = encodeFrame(type, sequence, payload);
    sequence = (sequence + 1) & 0xff;
    transmitQueue = transmitQueue.then(() => new Promise((resolve, reject) => {
      port.write(frame, error => error ? reject(error) : resolve());
    })).then(() => new Promise((resolve, reject) => {
      port.drain(error => error ? reject(error) : resolve());
    })).then(() => true, error => {
      console.warn(`Serial write failed: ${error.message}`);
      return false;
    });
    if (options.verbose) console.log(`TX type=0x${type.toString(16)} bytes=${frame.length}`);
    return transmitQueue;
  };

  const sendAlert = record => send(MessageType.ALERT_UPDATE,
    alertUpdatePayload(record.level, record.occurrence, record.id,
                       record.title, record.message));

  const removeAlert = id => send(MessageType.ALERT_REMOVE, alertIdPayload(id));

  const raiseTestAlert = key => {
    const template = alertTemplates[key];
    if (!template) return;
    const instance = nextTestAlertId++;
    const record = {
      ...template,
      id: `${template.id}.${instance}`,
      title: `${template.title} #${instance}`,
      occurrence: 1,
      snoozed: false,
      acknowledged: false,
      silenced: false,
      timer: null,
    };
    activeAlerts.set(record.id, record);
    void sendAlert(record);
    console.log(`Raised ${['INFO', 'WARNING', 'ALARM', 'EMERGENCY'][record.level]}: ${record.title} [${record.id}].`);
  };

  const clearTestAlerts = () => {
    for (const record of activeAlerts.values()) {
      if (record.timer) clearTimeout(record.timer);
      void removeAlert(record.id);
    }
    activeAlerts.clear();
    console.log('Cleared all simulated alert conditions.');
  };

  const handleAlertAction = actionMessage => {
    const record = activeAlerts.get(actionMessage.id);
    if (!record || record.occurrence !== actionMessage.occurrence) {
      console.log(`Ignored stale alert action for ${actionMessage.id}.`);
      return;
    }
    if (actionMessage.action === 0) {
      if (record.timer) clearTimeout(record.timer);
      record.acknowledged = true;
      record.snoozed = false;
      record.timer = null;
      void removeAlert(record.id);
      console.log(`Acknowledged ${record.id}; removed it from all displays while retaining the source condition.`);
    } else if (actionMessage.action === 1) {
      record.snoozed = true;
      void removeAlert(record.id);
      console.log(`Snoozed ${record.id} for ${snoozeDurationMs / 1000} seconds.`);
      record.timer = setTimeout(() => {
        if (!activeAlerts.has(record.id)) return;
        record.snoozed = false;
        record.silenced = false;
        record.occurrence = (record.occurrence + 1) >>> 0;
        record.timer = null;
        void sendAlert(record);
        console.log(`Snooze expired; repeated ${record.id} (occurrence ${record.occurrence}).`);
      }, snoozeDurationMs);
    } else if (actionMessage.action === 2) {
      record.silenced = true;
      void send(MessageType.ALERT_SILENCE, alertIdPayload(record.id));
      console.log(`Silenced ${record.id} on all displays; visual alert remains active.`);
    }
  };

  const availableConfigurations = async () => {
    const entries = await readdir(options.configDirectory, { withFileTypes: true });
    return entries
      .filter(entry => entry.isFile() && entry.name.toLowerCase().endsWith('.json'))
      .map(entry => entry.name)
      .filter(name => Buffer.byteLength(name, 'utf8') <= maximumConfigNameBytes)
      .sort((left, right) => left.localeCompare(right));
  };

  const sendConfigList = async () => {
    const names = await availableConfigurations();
    listId = (listId + 1) & 0xffff;
    const begin = Buffer.alloc(4);
    begin.writeUInt16LE(listId, 0);
    begin.writeUInt16LE(names.length, 2);
    if (!await send(MessageType.CONFIG_LIST_BEGIN, begin)) throw new Error('serial write failed');
    for (const [index, name] of names.entries()) {
      const encoded = Buffer.from(name, 'utf8');
      const item = Buffer.alloc(5 + encoded.length);
      item.writeUInt16LE(listId, 0);
      item.writeUInt16LE(index, 2);
      item[4] = encoded.length;
      encoded.copy(item, 5);
      if (!await send(MessageType.CONFIG_LIST_ITEM, item)) throw new Error('serial write failed');
    }
    const end = Buffer.alloc(2);
    end.writeUInt16LE(listId, 0);
    if (!await send(MessageType.CONFIG_LIST_END, end)) throw new Error('serial write failed');
    console.log(`Sent ${names.length} available configuration(s).`);
  };

  const sendConfiguration = async name => {
    if (name !== path.basename(name) || !name.toLowerCase().endsWith('.json')) {
      throw new Error('invalid configuration filename');
    }
    const names = await availableConfigurations();
    if (!names.includes(name)) throw new Error(`configuration not found: ${name}`);
    const contents = await readFile(path.join(options.configDirectory, name));
    if (contents.length === 0 || contents.length > maximumConfigBytes) {
      throw new Error(`${name} is empty or exceeds ${maximumConfigBytes} bytes`);
    }
    transferId = (transferId + 1) & 0xffff;
    const encodedName = Buffer.from(name, 'utf8');
    const begin = Buffer.alloc(11 + encodedName.length);
    begin.writeUInt16LE(transferId, 0);
    begin.writeUInt32LE(contents.length, 2);
    begin.writeUInt32LE(crc32(contents), 6);
    begin[10] = encodedName.length;
    encodedName.copy(begin, 11);
    if (!await send(MessageType.CONFIG_BEGIN, begin)) throw new Error('serial write failed');
    for (let offset = 0; offset < contents.length; offset += configChunkBytes) {
      const bytes = contents.subarray(offset, offset + configChunkBytes);
      const chunk = Buffer.alloc(6 + bytes.length);
      chunk.writeUInt16LE(transferId, 0);
      chunk.writeUInt32LE(offset, 2);
      bytes.copy(chunk, 6);
      if (!await send(MessageType.CONFIG_CHUNK, chunk)) throw new Error('serial write failed');
      await delay(configFrameGapMs);
    }
    const end = Buffer.alloc(2);
    end.writeUInt16LE(transferId, 0);
    if (!await send(MessageType.CONFIG_END, end)) throw new Error('serial write failed');
    console.log(`Sent configuration ${name} (${contents.length} bytes).`);
  };

  port.on('open', () => {
    console.log(`Connected to ${options.path} at ${options.baudRate} baud.`);
    console.log('Alert keys: [i] info  [w] warning  [a] alarm  [e] emergency  [x] clear all');
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
          for (const record of activeAlerts.values()) {
            if (!record.snoozed && !record.acknowledged) {
              void sendAlert(record).then(() => {
                if (record.silenced) {
                  return send(MessageType.ALERT_SILENCE, alertIdPayload(record.id));
                }
                return true;
              });
            }
          }
          if (options.pushList && !unsolicitedListSent) {
            unsolicitedListSent = true;
            void sendConfigList().catch(error => {
              console.warn(`Could not push configuration list: ${error.message}`);
            });
          }
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
        } else if (message.type === MessageType.CONFIG_LIST_REQUEST && message.payload.length === 0) {
          void sendConfigList().catch(error => {
            console.warn(`Could not send configuration list: ${error.message}`);
          });
        } else if (message.type === MessageType.CONFIG_REQUEST &&
                   message.payload.length >= 1 &&
                   message.payload[0] === message.payload.length - 1) {
          const name = message.payload.subarray(1).toString('utf8');
          console.log(`Display requested configuration ${name}.`);
          void sendConfiguration(name).catch(error => {
            console.warn(`Could not send configuration: ${error.message}`);
          });
        } else if (message.type === MessageType.ALERT_ACTION) {
          handleAlertAction(parseAlertAction(message.payload));
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
      console.log('Waiting for remote-a: no bytes received yet.');
    } else if (receivedFrames === 0) {
      console.log(`Received ${receivedBytes} byte(s), but no valid protocol frame (${decoder.errors} decode error(s)). Check baud and firmware version.`);
    } else {
      console.log('Display is responding, but has not sent a subscription snapshot yet.');
    }
    waitingNoticeShown = true;
  }, 2500);

  if (process.stdin.isTTY) {
    readline.emitKeypressEvents(process.stdin);
    process.stdin.setRawMode(true);
    process.stdin.resume();
    process.stdin.on('keypress', (text, key) => {
      if (key?.ctrl && key.name === 'c') {
        shutdown();
      } else if (text === 'x') {
        clearTestAlerts();
      } else if (alertTemplates[text]) {
        raiseTestAlert(text);
      }
    });
  }

  const shutdown = () => {
    clearInterval(valueTimer);
    clearInterval(pingTimer);
    clearInterval(helloTimer);
    clearInterval(statusTimer);
    for (const record of activeAlerts.values()) if (record.timer) clearTimeout(record.timer);
    if (process.stdin.isTTY) process.stdin.setRawMode(false);
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
    await mkdir(options.configDirectory, { recursive: true });
    if (!options.path) options.path = await detectPort();
    console.log(`Configuration directory: ${options.configDirectory}`);
    run(options);
  }
} catch (error) {
  console.error(error.message);
  usage();
  process.exitCode = 1;
}
