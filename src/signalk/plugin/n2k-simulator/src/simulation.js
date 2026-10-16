'use strict';

const { Geodesic } = require('geographiclib-geodesic');
const { Fluctuation, timescales } = require('./fluctuation');

const radians = degrees => degrees * Math.PI / 180;
const degrees = angle => angle * 180 / Math.PI;
const normalize = angle => ((angle % (2 * Math.PI)) + 2 * Math.PI) % (2 * Math.PI);
const signed = angle => normalize(angle + Math.PI) - Math.PI;
const defaults = Object.freeze({
  mode: 'marina',
  marinaHeading: 225,
  departureLatitude: 50.8,
  departureLongitude: -1.3,
  destinationLatitude: 50.75,
  destinationLongitude: -1.1,
  sailingSpeed: 2.5,
  engineRunning: false,
  engineRpm: 1800,
  windSpeed: 6,
  windDirection: 225,
  currentSpeed: 0.2,
  currentDirection: 90,
  waveHeight: 0.5,
  wavePeriod: 5,
  airTemperature: 18,
  waterTemperature: 15,
  coolantTemperature: 80,
  oilPressure: 350000,
  alternatorVoltage: 14.2,
  atmosphericPressure: 101325,
  depth: 12,
  chainLength: 40,
  anchorRadius: 200
});

const ranges = {
  marinaHeading: [0, 360],
  departureLatitude: [-89, 89], destinationLatitude: [-89, 89],
  departureLongitude: [-180, 180], destinationLongitude: [-180, 180],
  sailingSpeed: [0, 15], engineRpm: [0, 6000], windSpeed: [0, 60],
  windDirection: [0, 360], currentSpeed: [0, 10], currentDirection: [0, 360],
  waveHeight: [0, 15], wavePeriod: [1, 30], airTemperature: [-30, 60],
  waterTemperature: [-2, 45], coolantTemperature: [-20, 150],
  depth: [0, 1000], chainLength: [0, 200], anchorRadius: [0, 200], oilPressure: [0, 1000000],
  alternatorVoltage: [0, 32], atmosphericPressure: [80000, 110000]
};

function validate(patch) {
  if (!patch || typeof patch !== 'object' || Array.isArray(patch)) {
    throw new TypeError('Controls must be an object');
  }
  for (const [key, value] of Object.entries(patch)) {
    if (key === 'mode') {
      if (!['marina', 'anchor', 'underway'].includes(value)) throw new RangeError('Invalid mode');
    } else if (key === 'engineRunning') {
      if (typeof value !== 'boolean') throw new TypeError('engineRunning must be boolean');
    } else if (key === 'fluctuations') {
      if (!value || typeof value !== 'object' || Array.isArray(value)) throw new TypeError('Fluctuations must be an object');
      for (const [parameter, bounds] of Object.entries(value)) {
        if (!timescales[parameter]) throw new RangeError(`Unknown fluctuating control: ${parameter}`);
        if (bounds === null) continue;
        if (!bounds || Object.keys(bounds).some(field => !['min', 'max'].includes(field))) throw new TypeError('Expected min/max bounds');
        validate({ [parameter]: bounds.min });
        validate({ [parameter]: bounds.max });
        if (bounds.min > bounds.max) throw new RangeError(`${parameter}: minimum exceeds maximum`);
      }
    } else if (key === 'position') {
      if (!value || Object.keys(value).some(field => !['latitude', 'longitude'].includes(field))) throw new TypeError('Expected latitude/longitude');
      validate({ departureLatitude: value.latitude, departureLongitude: value.longitude });
    } else {
      const range = ranges[key];
      if (!range) throw new RangeError(`Unknown control: ${key}`);
      if (!Number.isFinite(value) || value < range[0] || value > range[1]) {
        throw new RangeError(`${key} must be between ${range[0]} and ${range[1]}`);
      }
    }
  }
}

function anchorSwingRadius(controls) {
  return Math.min(controls.anchorRadius, Math.sqrt(Math.max(0, controls.chainLength ** 2 - controls.depth ** 2)));
}

class Simulation {
  constructor(controls = {}) {
    this.controls = { ...defaults };
    this.fluctuations = {};
    this.reset();
    this.configure(controls);
  }

  configure(patch) {
    validate(patch);
    const { fluctuations, position, ...controls } = patch;
    this.controls = { ...this.controls, ...controls };
    if (fluctuations) {
      for (const [key, bounds] of Object.entries(fluctuations)) {
        if (bounds === null) delete this.fluctuations[key];
        else this.fluctuations[key] = { ...bounds };
        delete this.fluctuators[key];
      }
    }
    if ('departureLatitude' in patch || 'departureLongitude' in patch) this.reset();
    if (position) {
      this.latitude = position.latitude;
      this.longitude = position.longitude;
      this.arrived = false;
    }
    if (this.controls.mode === 'marina') this.heading = normalize(radians(this.controls.marinaHeading));
    return this.snapshot();
  }

  reset() {
    this.elapsed = 0;
    this.engineHours = 0;
    this.speed = 0;
    this.latitude = this.controls.departureLatitude;
    this.longitude = this.controls.departureLongitude;
    this.heading = normalize(radians(this.controls.mode === 'marina' ? this.controls.marinaHeading : this.controls.windDirection));
    this.anchorEastVelocity = 0;
    this.anchorNorthVelocity = 0;
    this.arrived = false;
    this.fluctuators = {};
    this.wavePhase = 0;
    this.seaEnvelope = new Fluctuation('waveHeight', 0.85);
    this.effective = { ...this.controls };
  }

  sampleControls() {
    this.effective = { ...this.controls };
    for (const [key, bounds] of Object.entries(this.fluctuations)) {
      this.fluctuators[key] ??= new Fluctuation(key, Math.max(bounds.min, Math.min(bounds.max, this.controls[key])), this.elapsed);
      this.effective[key] = this.fluctuators[key].sample(this.elapsed, key, bounds);
    }
    return this.effective;
  }

  step(seconds) {
    if (!Number.isFinite(seconds) || seconds <= 0 || seconds > 5) {
      throw new RangeError('Step must be greater than zero and at most five seconds');
    }
    this.elapsed += seconds;
    const controls = this.sampleControls();
    this.wavePhase = normalize(this.wavePhase + seconds * 2 * Math.PI / controls.wavePeriod);
    if (controls.engineRunning) this.engineHours += seconds / 3600;
    const route = Geodesic.WGS84.Inverse(this.latitude, this.longitude,
      controls.destinationLatitude, controls.destinationLongitude);
    this.arrived = route.s12 < 5;
    const targetSpeed = controls.mode === 'underway' && !this.arrived
      ? (controls.engineRunning ? controls.engineRpm / 600 : controls.sailingSpeed) : 0;
    this.speed += (targetSpeed - this.speed) * (1 - Math.exp(-seconds / 8));
    const currentAngle = radians(controls.currentDirection);
    if (controls.mode === 'underway' && !this.arrived) {
      this.heading = normalize(radians(route.azi1));
      const east = this.speed * Math.sin(this.heading) + controls.currentSpeed * Math.sin(currentAngle);
      const north = this.speed * Math.cos(this.heading) + controls.currentSpeed * Math.cos(currentAngle);
      const distance = Math.min(Math.hypot(east, north) * seconds, route.s12);
      const point = Geodesic.WGS84.Direct(this.latitude, this.longitude,
        degrees(Math.atan2(east, north)), distance);
      this.latitude = point.lat2;
      this.longitude = point.lon2;
    } else if (controls.mode === 'anchor') {
      this.speed = 0;
      const tension = controls.windSpeed ** 2 / (controls.windSpeed ** 2 + 16);
      const swing = radians(12) * tension * Math.sin(this.elapsed / 30);
      const wind = radians(controls.windDirection);
      const targetHeading = normalize(wind + swing);
      this.heading = normalize(this.heading + signed(targetHeading - this.heading) * (1 - Math.exp(-seconds / 8)));
      const maximumRadius = anchorSwingRadius(controls);
      const distance = maximumRadius * tension * (0.9 + 0.1 * Math.cos(this.elapsed / 45));
      const bearing = wind + Math.PI + swing;
      const offset = Geodesic.WGS84.Inverse(controls.departureLatitude, controls.departureLongitude,
        this.latitude, this.longitude);
      const relaxation = 1 - Math.exp(-seconds / 20);
      let east = offset.s12 * Math.sin(radians(offset.azi1));
      let north = offset.s12 * Math.cos(radians(offset.azi1));
      east += (distance * Math.sin(bearing) - east) * relaxation;
      north += (distance * Math.cos(bearing) - north) * relaxation;
      const radius = Math.hypot(east, north);
      const point = Geodesic.WGS84.Direct(controls.departureLatitude, controls.departureLongitude,
        degrees(Math.atan2(east, north)), Math.min(radius, maximumRadius));
      const movement = Geodesic.WGS84.Inverse(this.latitude, this.longitude, point.lat2, point.lon2);
      this.anchorEastVelocity = movement.s12 / seconds * Math.sin(radians(movement.azi1));
      this.anchorNorthVelocity = movement.s12 / seconds * Math.cos(radians(movement.azi1));
      this.latitude = point.lat2;
      this.longitude = point.lon2;
    } else {
      this.speed = 0;
      if (controls.mode === 'marina') this.heading = normalize(radians(controls.marinaHeading));
    }
    return this.snapshot();
  }

  snapshot() {
    const controls = this.sampleControls();
    const windSpeed = controls.windSpeed;
    const windDirection = normalize(radians(controls.windDirection));
    const moving = controls.mode === 'underway' && !this.arrived;
    const anchored = controls.mode === 'anchor';
    const east = moving ? this.speed * Math.sin(this.heading) + controls.currentSpeed * Math.sin(radians(controls.currentDirection)) : anchored ? this.anchorEastVelocity : 0;
    const north = moving ? this.speed * Math.cos(this.heading) + controls.currentSpeed * Math.cos(radians(controls.currentDirection)) : anchored ? this.anchorNorthVelocity : 0;
    const apparentEast = windSpeed * Math.sin(windDirection) + east;
    const apparentNorth = windSpeed * Math.cos(windDirection) + north;
    const waveAmplitude = this.seaEnvelope.sample(this.elapsed, 'waveHeight', { min: 0.65, max: 1 });
    return {
      controls: { ...this.controls }, effectiveControls: { ...controls },
      fluctuations: structuredClone(this.fluctuations), elapsed: this.elapsed, arrived: this.arrived,
      latitude: this.latitude, longitude: this.longitude, heading: this.heading,
      anchorSwingRadius: anchorSwingRadius(controls),
      anchorChainTooShort: controls.chainLength < controls.depth,
      anchorDistance: anchored ? Geodesic.WGS84.Inverse(controls.departureLatitude,
        controls.departureLongitude, this.latitude, this.longitude).s12 : 0,
      speedThroughWater: this.speed, speedOverGround: Math.hypot(east, north),
      courseOverGround: Math.hypot(east, north) > 0.000001 ? normalize(Math.atan2(east, north)) : this.heading,
      trueWindSpeed: windSpeed, trueWindDirection: windDirection,
      apparentWindSpeed: Math.hypot(apparentEast, apparentNorth),
      apparentWindAngle: signed(Math.atan2(apparentEast, apparentNorth) - this.heading),
      roll: controls.waveHeight * 0.04 * waveAmplitude * Math.sin(this.wavePhase),
      pitch: controls.waveHeight * 0.025 * waveAmplitude * Math.sin(this.wavePhase + 1),
      depth: controls.depth,
      engineRpm: controls.engineRunning ? controls.engineRpm : 0,
      engineHours: this.engineHours,
      coolantTemperature: controls.engineRunning ? controls.coolantTemperature + 273.15 : controls.airTemperature + 273.15,
      oilPressure: controls.engineRunning ? controls.oilPressure : 0,
      alternatorVoltage: controls.engineRunning ? controls.alternatorVoltage : 12.6,
      atmosphericPressure: controls.atmosphericPressure,
      airTemperature: controls.airTemperature + 273.15,
      waterTemperature: controls.waterTemperature + 273.15
    };
  }
}

module.exports = { Simulation, defaults, ranges };