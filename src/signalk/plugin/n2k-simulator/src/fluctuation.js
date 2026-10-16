'use strict';

const timescales = {
  sailingSpeed: [20, 90], engineRpm: [3, 15], windSpeed: [8, 35],
  windDirection: [15, 60], currentSpeed: [120, 600], currentDirection: [180, 900],
  waveHeight: [30, 120], wavePeriod: [60, 180], airTemperature: [120, 600],
  waterTemperature: [300, 1200], coolantTemperature: [30, 180],
  depth: [10, 60], anchorRadius: [20, 90], oilPressure: [3, 15],
  alternatorVoltage: [5, 30], atmosphericPressure: [300, 1200]
};

class Fluctuation {
  constructor(key, initial, time = 0) {
    this.seed = [...key].reduce((seed, character) => (Math.imul(seed, 31) + character.charCodeAt(0)) >>> 0, 12345);
    this.from = initial;
    this.target = initial;
    this.start = time;
    this.end = time;
  }

  random() {
    this.seed = (Math.imul(this.seed, 1664525) + 1013904223) >>> 0;
    return this.seed / 4294967296;
  }

  sample(time, key, bounds) {
    while (time >= this.end) {
      this.from = this.target;
      this.start = this.end;
      const [minimum, maximum] = timescales[key];
      this.end += minimum + this.random() * (maximum - minimum);
      this.target = bounds.min + this.random() * (bounds.max - bounds.min);
    }
    const progress = (time - this.start) / (this.end - this.start);
    const blend = progress * progress * (3 - 2 * progress);
    return this.from + (this.target - this.from) * blend;
  }
}

module.exports = { Fluctuation, timescales };