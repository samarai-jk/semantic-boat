'use strict';

const { pgnToActisenseSerialFormat } = require('@canboat/canboatjs');

const instruments = [
  { pgn: 127250, period: 0.1, fields: state => ({ SID: 0, Heading: state.heading, Reference: 'True' }),
    values: state => ({ 'navigation.headingTrue': state.heading }) },
  { pgn: 127257, period: 0.1, fields: state => ({ SID: 0, Roll: state.roll, Pitch: state.pitch, Yaw: state.heading }),
    values: state => ({ 'navigation.attitude': { roll: state.roll, pitch: state.pitch, yaw: state.heading } }) },
  { pgn: 129025, period: 0.2, fields: state => ({ Latitude: state.latitude, Longitude: state.longitude }),
    values: state => ({ 'navigation.position': { latitude: state.latitude, longitude: state.longitude } }) },
  { pgn: 129026, period: 0.2, fields: state => ({ SID: 0, 'COG Reference': 'True', COG: state.courseOverGround, SOG: state.speedOverGround }),
    values: state => ({ 'navigation.courseOverGroundTrue': state.courseOverGround, 'navigation.speedOverGround': state.speedOverGround }) },
  { pgn: 128259, period: 0.5, fields: state => ({ SID: 0, 'Speed Water Referenced': state.speedThroughWater, 'Speed Water Referenced Type': 'Paddle wheel' }),
    values: state => ({ 'navigation.speedThroughWater': state.speedThroughWater }) },
  { pgn: 128267, period: 1, fields: state => ({ SID: 0, Depth: state.depth, Offset: 0 }),
    values: state => ({ 'environment.depth.belowTransducer': state.depth }) },
  { pgn: 130306, period: 0.1, fields: state => ({ SID: 0, 'Wind Speed': state.apparentWindSpeed, 'Wind Angle': (state.apparentWindAngle + 2 * Math.PI) % (2 * Math.PI), Reference: 'Apparent' }),
    values: state => ({ 'environment.wind.speedApparent': state.apparentWindSpeed, 'environment.wind.angleApparent': state.apparentWindAngle }) },
  { pgn: 130306, period: 0.5, fields: state => ({ SID: 0, 'Wind Speed': state.trueWindSpeed, 'Wind Angle': state.trueWindDirection, Reference: 'True (ground referenced to North)' }),
    values: state => ({ 'environment.wind.speedTrue': state.trueWindSpeed, 'environment.wind.directionTrue': state.trueWindDirection }) },
  { pgn: 127488, period: 0.1, fields: state => ({ Instance: 0, Speed: state.engineRpm }),
    values: state => ({ 'propulsion.main.revolutions': state.engineRpm / 60 }) },
  { pgn: 127489, period: 0.5, fields: state => ({ Instance: 0, 'Oil pressure': state.oilPressure, Temperature: state.coolantTemperature, 'Alternator Potential': state.alternatorVoltage, 'Total Engine hours': state.engineHours * 3600 }),
    values: state => ({ 'propulsion.main.oilPressure': state.oilPressure, 'propulsion.main.coolantTemperature': state.coolantTemperature, 'propulsion.main.alternatorVoltage': state.alternatorVoltage, 'propulsion.main.runTime': state.engineHours * 3600 }) },
  { pgn: 130310, period: 1, fields: state => ({ SID: 0, 'Water Temperature': state.waterTemperature, 'Outside Ambient Air Temperature': state.airTemperature, 'Atmospheric Pressure': state.atmosphericPressure }),
    values: state => ({ 'environment.water.temperature': state.waterTemperature, 'environment.outside.temperature': state.airTemperature, 'environment.outside.pressure': state.atmosphericPressure }) }
];

const readings = [
  ['navigation.headingTrue', 'Heading', 0, 'Heading'],
  ['navigation.attitude.roll', 'Roll', 1, 'Roll'],
  ['navigation.attitude.pitch', 'Pitch', 1, 'Pitch'],
  ['navigation.attitude.yaw', 'Yaw', 1, 'Yaw'],
  ['navigation.position', 'Position', 2, 'Latitude', 'Longitude'],
  ['navigation.courseOverGroundTrue', 'Course over ground', 3, 'COG'],
  ['navigation.speedOverGround', 'Speed over ground', 3, 'SOG'],
  ['navigation.speedThroughWater', 'Speed through water', 4, 'Speed Water Referenced'],
  ['environment.depth.belowTransducer', 'Depth', 5, 'Depth'],
  ['environment.wind.speedApparent', 'Apparent wind speed', 6, 'Wind Speed'],
  ['environment.wind.angleApparent', 'Apparent wind angle', 6, 'Wind Angle'],
  ['environment.wind.speedTrue', 'True wind speed', 7, 'Wind Speed'],
  ['environment.wind.directionTrue', 'True wind direction', 7, 'Wind Angle'],
  ['propulsion.main.revolutions', 'Engine RPM', 8, 'Speed'],
  ['propulsion.main.oilPressure', 'Oil pressure', 9, 'Oil pressure'],
  ['propulsion.main.coolantTemperature', 'Coolant temperature', 9, 'Temperature'],
  ['propulsion.main.alternatorVoltage', 'Alternator voltage', 9, 'Alternator Potential'],
  ['propulsion.main.runTime', 'Engine hours', 9, 'Total Engine hours'],
  ['environment.water.temperature', 'Water temperature', 10, 'Water Temperature'],
  ['environment.outside.temperature', 'Air temperature', 10, 'Outside Ambient Air Temperature'],
  ['environment.outside.pressure', 'Atmospheric pressure', 10, 'Atmospheric Pressure']
];

function validateOutputs(outputs) {
  if (!outputs || typeof outputs !== 'object' || Array.isArray(outputs)) throw new TypeError('Outputs must be an object');
  for (const [path, enabled] of Object.entries(outputs)) {
    if (!readings.some(reading => reading[0] === path)) throw new RangeError(`Unknown output: ${path}`);
    if (typeof enabled !== 'boolean') throw new TypeError(`${path}: enabled must be boolean`);
  }
}

class InstrumentScheduler {
  constructor(sourceAddress = 200) {
    if (!Number.isInteger(sourceAddress) || sourceAddress < 0 || sourceAddress > 251) {
      throw new RangeError('Source address must be between 0 and 251');
    }
    this.sourceAddress = sourceAddress;
    this.next = instruments.map(() => 0);
  }

  sample(state, timestamp = new Date().toISOString()) {
    const messages = [];
    for (const [index, instrument] of instruments.entries()) {
      if (state.elapsed + 1e-9 < this.next[index]) continue;
      const missed = Math.floor((state.elapsed + 1e-9 - this.next[index]) / instrument.period);
      this.next[index] += (missed + 1) * instrument.period;
      const fields = instrument.fields(state);
      const values = instrument.values(state);
      const enabled = readings.filter(reading => reading[2] === index && state.outputs?.[reading[0]] !== false);
      if (!enabled.length) continue;
      for (const [path, , owner, ...names] of readings) {
        if (owner !== index || state.outputs?.[path] !== false) continue;
        for (const name of names) delete fields[name];
        if (path.startsWith('navigation.attitude.')) delete values['navigation.attitude'][path.split('.').at(-1)];
        else delete values[path];
      }
      const line = pgnToActisenseSerialFormat({
        pgn: instrument.pgn, prio: instrument.pgn === 127488 || instrument.pgn === 127489 ? 2 : 3,
        src: this.sourceAddress, dst: 255, timestamp, fields
      });
      if (!line) throw new Error(`Encoding PGN ${instrument.pgn} failed`);
      messages.push({ pgn: instrument.pgn, line, values });
    }
    return messages;
  }
}

function delta(messages, timestamp) {
  return {
    context: 'vessels.self',
    updates: [{ $source: 'semantic-boat.n2k-simulator', timestamp,
      values: messages.flatMap(message => Object.entries(message.values).map(([path, value]) => ({ path, value }))) }]
  };
}

module.exports = { InstrumentScheduler, instruments, delta, readings, validateOutputs };