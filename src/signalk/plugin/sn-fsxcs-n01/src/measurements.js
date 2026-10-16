'use strict';

const definitions = [
  ['wind.speed', 'm/s', 'Station wind speed', 'Measured airflow; not navigation true wind.'],
  ['wind.force', null, 'Station wind-force grade', 'Manufacturer wind-force grade, not a separately measured quantity.'],
  ['wind.directionSector', null, 'Station wind sector', '0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW; reference unverified.'],
  ['wind.direction', 'rad', 'Station wind direction', 'Manufacturer north-based angle; compass correction and north reference unverified.'],
  ['air.relativeHumidity', 'ratio', 'Station relative humidity', 'Relative humidity as a fraction.'],
  ['air.temperature', 'K', 'Station air temperature', 'Signed sensor temperature converted to kelvin.'],
  ['air.noiseLevel', 'dB', 'Station noise level', 'Frequency weighting is not specified by this model manual.'],
  ['air.pm25', 'kg/m3', 'Station PM2.5', 'PM2.5 mass concentration; register 507 is CO2 on alternate variants.'],
  ['air.pm10', 'kg/m3', 'Station PM10', 'PM10 mass concentration.'],
  ['air.pressure', 'Pa', 'Station pressure', 'Measured pressure; not corrected to sea level.'],
  ['light.illuminance', 'lux', 'Station illuminance', 'Unsigned 32-bit illuminance assembled high word first.'],
  ['light.coarseIlluminance', 'lux', 'Station coarse illuminance', 'Alternate illuminance representation with 100-lux resolution.'],
  ['rain.accumulation', 'm', 'Station accumulated rainfall', 'Station cumulative counter; reset, power-loss retention and rollover are unverified.'],
  ['compass.angle', 'rad', 'Station compass angle', 'Station electronic compass; not established as true or magnetic vessel heading.'],
  ['solar.irradiance', 'W/m2', 'Station solar irradiance', 'Broad-band solar irradiance, not UV irradiance or UV index.']
];

function prefix(rootPath = 'environment.weather') {
  if (typeof rootPath !== 'string' || !/^environment(?:\.[a-zA-Z][a-zA-Z0-9]*)+$/.test(rootPath)) {
    throw new Error('Root path must be a custom environment namespace with nonempty letter/digit segments');
  }
  const reserved = ['outside', 'inside', 'water', 'depth', 'current', 'tide', 'heave', 'wind', 'time', 'mode'];
  if (reserved.includes(rootPath.split('.')[1])) throw new Error('Root path overlaps a protected Signal K instrument namespace');
  return rootPath;
}

function metadata(rootPath) {
  return definitions.map(([suffix, units, displayName, description]) => ({
    path: `${prefix(rootPath)}.${suffix}`,
    value: { displayName, description, ...(units ? { units } : {}),
      ...(['air.pm25', 'air.pm10'].includes(suffix) ? { displayUnits: {
        category: 'custom', targetUnit: 'ug/m3', formula: 'value * 1000000000',
        inverseFormula: 'value / 1000000000', symbol: '\u00b5g/m\u00b3', displayFormat: '0'
      } } : {}) }
  }));
}

function decode(words, rootPath) {
  if (!Array.isArray(words) || words.length !== 16 ||
      words.some(word => !Number.isInteger(word) || word < 0 || word > 0xFFFF)) {
    throw new Error('Expected exactly 16 unsigned measurement words');
  }
  const buffer = Buffer.alloc(32);
  words.forEach((word, index) => buffer.writeUInt16BE(word, index * 2));
  const values = [
    words[0] <= 400 ? words[0] / 10 : null,
    words[1],
    words[2] <= 7 ? words[2] : null,
    words[3] < 360 ? words[3] * Math.PI / 180 : null,
    words[4] <= 990 ? words[4] / 1000 : null,
    buffer.readInt16BE(10) >= -400 && buffer.readInt16BE(10) <= 800
      ? buffer.readInt16BE(10) / 10 + 273.15 : null,
    words[6] >= 300 && words[6] <= 1200 ? words[6] / 10 : null,
    words[7] <= 1000 ? words[7] * 1e-9 : null,
    words[8] <= 1000 ? words[8] * 1e-9 : null,
    words[9] <= 1200 ? words[9] * 100 : null,
    buffer.readUInt32BE(20) <= 200000 ? buffer.readUInt32BE(20) : null,
    words[12] <= 2000 ? words[12] * 100 : null,
    words[13] / 10000,
    words[14] < 36000 ? words[14] * Math.PI / 18000 : null,
    words[15] <= 1800 ? words[15] : null
  ];
  return definitions.map(([suffix], index) => ({ path: `${prefix(rootPath)}.${suffix}`, value: values[index] }));
}

function unavailable(rootPath) {
  return definitions.map(([suffix]) => ({ path: `${prefix(rootPath)}.${suffix}`, value: null }));
}

module.exports = { prefix, metadata, decode, unavailable };