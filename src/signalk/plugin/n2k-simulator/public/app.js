'use strict';

const byId = id => document.getElementById(id);
const api = new URL('../', window.location.href);
const routeKeys = ['departureLatitude', 'departureLongitude', 'destinationLatitude', 'destinationLongitude'];
const controls = [
  ['engine-controls', 'engineRpm', 'Engine speed', 'rpm', 0, 6000, 50],
  ['engine-controls', 'sailingSpeed', 'Sailing speed', 'kn', 0, 29, 0.1, 1 / 1.943844],
  ['engine-controls', 'oilPressure', 'Oil pressure', 'bar', 0, 10, 0.1, 100000],
  ['engine-controls', 'alternatorVoltage', 'Alternator voltage', 'V', 0, 32, 0.1],
  ['wind-controls', 'windSpeed', 'True wind speed', 'kn', 0, 60, 0.5, 1 / 1.943844],
  ['wind-controls', 'windDirection', 'Wind from', 'deg T', 0, 360, 1],
  ['wind-controls', 'currentSpeed', 'Current speed', 'kn', 0, 19, 0.1, 1 / 1.943844],
  ['wind-controls', 'currentDirection', 'Current toward', 'deg T', 0, 360, 1],
  ['sea-controls', 'waveHeight', 'Wave height', 'm', 0, 15, 0.1],
  ['sea-controls', 'wavePeriod', 'Wave period', 's', 1, 30, 1],
  ['sea-controls', 'depth', 'Depth below transducer', 'm', 0, 1000, 0.5],
  ['temperature-controls', 'airTemperature', 'Air', 'deg C', -30, 60, 0.5],
  ['temperature-controls', 'waterTemperature', 'Water', 'deg C', -2, 45, 0.5],
  ['engine-controls', 'coolantTemperature', 'Engine coolant', 'deg C', -20, 150, 1],
  ['temperature-controls', 'atmosphericPressure', 'Atmospheric pressure', 'hPa', 800, 1100, 1, 100]
];
let state = null;
let routeDirty = false;
let selection = null;
let fitted = false;
const pending = new Map();
const timers = new Map();
let metadata = null;
const outputSwitches = new Map();
const rangeSliders = new Map();
const editingRanges = new Set();
const controlOperations = new Set();
const presetStorageKey = `semantic-boat.n2k-simulator.presets:${api.pathname}`;
let presets = [];
let presetBusy = false;
const controlOutputs = {
  engineRpm: 'propulsion.main.revolutions', sailingSpeed: 'navigation.speedThroughWater',
  oilPressure: 'propulsion.main.oilPressure', alternatorVoltage: 'propulsion.main.alternatorVoltage',
  windSpeed: 'environment.wind.speedTrue', windDirection: 'environment.wind.directionTrue',
  depth: 'environment.depth.belowTransducer', airTemperature: 'environment.outside.temperature',
  waterTemperature: 'environment.water.temperature', coolantTemperature: 'propulsion.main.coolantTemperature',
  atmosphericPressure: 'environment.outside.pressure'
};
const derivedReadings = {
  'navigation.attitude.roll': next => `${(next.roll * 180 / Math.PI).toFixed(1)} deg`,
  'navigation.attitude.pitch': next => `${(next.pitch * 180 / Math.PI).toFixed(1)} deg`,
  'navigation.attitude.yaw': next => `${(next.heading * 180 / Math.PI).toFixed(0)} deg`,
  'navigation.courseOverGroundTrue': next => `${(next.courseOverGround * 180 / Math.PI).toFixed(0)} deg T`,
  'environment.wind.angleApparent': next => `${(next.apparentWindAngle * 180 / Math.PI).toFixed(0)} deg`,
  'propulsion.main.runTime': next => `${next.engineHours.toFixed(3)} h`
};
const derivedOutputs = new Map();
let draggingBoat = false;
let draggingDestination = false;

for (const [parent, key, label, unit, min, max, step, factor = 1] of controls) {
  const row = document.createElement('div');
  row.className = 'control-row';
  row.innerHTML = `<div class="control-label"><span>${label}</span><div class="reading-value"${controlOutputs[key] ? ` data-output="${controlOutputs[key]}"` : ''}><output id="${key}-live"></output></div></div><div class="range-state"><span id="${key}-mode">Fixed</span><span>${unit}</span></div><div id="${key}-range" class="dual-range"></div><div class="bounds-fields"><label>Min<input id="${key}-min" type="number" min="${min}" max="${max}" step="any" aria-label="${label} minimum"></label><label>Max<input id="${key}-max" type="number" min="${min}" max="${max}" step="any" aria-label="${label} maximum"></label></div>`;
  byId(parent).append(row);
  noUiSlider.create(byId(`${key}-range`), {
    start: [min, min], connect: [false, true, false], step: 0.0001, animate: false,
    keyboardMultiplier: step / 0.0001,
    format: { to: value => Number(value.toFixed(4)), from: Number },
    range: { min, max }, handleAttributes: [
      { 'aria-label': `${label} minimum handle` }, { 'aria-label': `${label} maximum handle` }
    ]
  });
  const slider = byId(`${key}-range`).noUiSlider;
  rangeSliders.set(key, slider);
  const update = values => {
    const bounds = values.map(Number);
    byId(`${key}-min`).value = bounds[0];
    byId(`${key}-max`).value = bounds[1];
    byId(`${key}-mode`).textContent = bounds[0] === bounds[1] ? 'Fixed' : 'Variable';
    pending.set(key, bounds);
    clearTimeout(timers.get(key));
    timers.set(key, setTimeout(async () => {
      const current = pending.get(key);
      const minimum = current[0] * factor;
      const maximum = current[1] * factor;
      const saved = await command('controls', { [key]: (minimum + maximum) / 2,
        fluctuations: { [key]: minimum === maximum ? null : { min: minimum, max: maximum } } });
      if (saved && pending.get(key) === current) pending.delete(key);
    }, 150));
  };
  slider.on('start', () => editingRanges.add(key));
  slider.on('end', () => editingRanges.delete(key));
  slider.on('slide', update);
  const updateBounds = () => {
    const minimum = byId(`${key}-min`);
    const maximum = byId(`${key}-max`);
    minimum.setCustomValidity('');
    maximum.setCustomValidity('');
    if (Number(minimum.value) > Number(maximum.value)) minimum.setCustomValidity('Minimum must not exceed maximum');
    if (!minimum.reportValidity() || !maximum.reportValidity() || minimum.value === '' || maximum.value === '') return;
    const values = [Number(minimum.value), Number(maximum.value)];
    slider.set(values);
    update(slider.get());
  };
  byId(`${key}-min`).onchange = updateBounds;
  byId(`${key}-max`).onchange = updateBounds;
}

const map = L.map('chart', { zoomControl: false }).setView([50.77, -1.2], 11);
L.control.zoom({ position: 'bottomleft' }).addTo(map);
const tiles = L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
  maxZoom: 18, referrerPolicy: 'strict-origin-when-cross-origin',
  attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a>'
}).addTo(map);
tiles.on('tileerror', () => { byId('map-status').hidden = false; });
tiles.on('tileload', () => { byId('map-status').hidden = true; });
const route = L.polyline([], { color: '#176b54', weight: 2, dashArray: '6 7' }).addTo(map);
const anchorZone = L.circle([50.8, -1.3], { radius: 15, color: '#b54d41', weight: 1, dashArray: '4 5', fillOpacity: 0.05 });
const departure = L.circleMarker([50.8, -1.3], { radius: 6, color: '#fff', weight: 2, fillColor: '#176b54', fillOpacity: 1 }).addTo(map).bindTooltip('Departure');
const destination = L.marker([50.75, -1.1], { draggable: true, autoPan: true,
  icon: L.divIcon({ className: '', html: '<div class="destination-marker"><i data-lucide="flag"></i></div>', iconSize: [28, 28], iconAnchor: [14, 14] }) }).addTo(map).bindTooltip('Destination');
const boat = L.marker([50.8, -1.3], { draggable: true, autoPan: true, icon: L.divIcon({ className: '', html: '<div class="vessel-marker"><i data-lucide="navigation"></i></div>', iconSize: [30, 30], iconAnchor: [15, 15] }) }).addTo(map).bindTooltip('Simulated vessel');
boat.on('dragstart', () => { draggingBoat = true; });
boat.on('dragend', async () => {
  const point = boat.getLatLng();
  const patch = state?.controls.mode === 'anchor'
    ? { departureLatitude: point.lat, departureLongitude: point.lng }
    : { position: { latitude: point.lat, longitude: point.lng } };
  await command('controls', patch);
  draggingBoat = false;
});
destination.on('dragstart', () => { draggingDestination = true; });
destination.on('dragend', async () => {
  const point = destination.getLatLng();
  routeDirty = false;
  await command('controls', { destinationLatitude: point.lat, destinationLongitude: point.lng });
  draggingDestination = false;
});
lucide.createIcons();

function error(message) {
  byId('error').textContent = message;
  byId('error').hidden = !message;
}

async function request(endpoint, body) {
  const response = await fetch(new URL(endpoint, api), body === undefined
    ? { cache: 'no-store' }
    : { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
  const result = await response.json();
  if (!response.ok) throw new Error(result.error || `HTTP ${response.status}`);
  return result;
}

async function command(endpoint, body = {}) {
  const operation = request(endpoint, body);
  controlOperations.add(operation);
  try { render(await operation); error(''); return true; }
  catch (failure) { error(failure.message); return false; }
  finally { controlOperations.delete(operation); }
}

function refreshPresets(selected = '') {
  const select = byId('preset-select');
  select.replaceChildren(new Option('Current settings', ''));
  for (const preset of presets) select.add(new Option(preset.name, preset.name));
  select.value = selected;
  byId('delete-preset').disabled = presetBusy || !select.value;
}

function persistPresets(next, selected) {
  localStorage.setItem(presetStorageKey, JSON.stringify({ version: 1, presets: next }));
  presets = next;
  refreshPresets(selected);
}

function setPresetBusy(busy) {
  presetBusy = busy;
  byId('save-preset').disabled = busy;
  byId('preset-select').disabled = busy;
  byId('delete-preset').disabled = busy || !byId('preset-select').value;
  byId('preset-form').querySelector('[type=submit]').disabled = busy;
}

async function settleControls() {
  for (const timer of timers.values()) clearTimeout(timer);
  await Promise.all([...controlOperations]);
  for (const [, key, , , , , , factor = 1] of controls) {
    const bounds = pending.get(key);
    if (!bounds) continue;
    const minimum = bounds[0] * factor;
    const maximum = bounds[1] * factor;
    await request('controls', { [key]: (minimum + maximum) / 2,
      fluctuations: { [key]: minimum === maximum ? null : { min: minimum, max: maximum } } });
    if (pending.get(key) === bounds) pending.delete(key);
  }
}

byId('save-preset').onclick = () => {
  byId('preset-name').value = byId('preset-select').value;
  byId('preset-replace').hidden = !presets.some(preset => preset.name === byId('preset-name').value.trim());
  byId('preset-error').hidden = true;
  byId('preset-dialog').showModal();
  byId('preset-name').focus();
};
byId('cancel-preset').onclick = () => byId('preset-dialog').close();
byId('preset-name').oninput = () => {
  byId('preset-name').setCustomValidity('');
  byId('preset-replace').hidden = !presets.some(preset => preset.name === byId('preset-name').value.trim());
};
byId('preset-form').onsubmit = async event => {
  event.preventDefault();
  if (presetBusy) return;
  const name = byId('preset-name').value.trim();
  if (!name) {
    byId('preset-name').setCustomValidity('Enter a preset name');
    byId('preset-name').reportValidity();
    return;
  }
  setPresetBusy(true);
  try {
    if (routeDirty) throw new Error('Apply the route before saving a preset.');
    await settleControls();
    const current = await request('state');
    const preset = { name, settings: { controls: current.controls, fluctuations: current.fluctuations,
      outputs: current.outputs, position: { latitude: current.latitude, longitude: current.longitude }, paused: current.paused } };
    persistPresets([...presets.filter(entry => entry.name !== name), preset].sort((left, right) => left.name.localeCompare(right.name)), name);
    render(current);
    byId('preset-dialog').close();
    error('');
  } catch (failure) {
    byId('preset-error').textContent = failure.message;
    byId('preset-error').hidden = false;
  } finally { setPresetBusy(false); }
};
byId('preset-select').onchange = async event => {
  const preset = presets.find(entry => entry.name === event.target.value);
  byId('delete-preset').disabled = !preset;
  if (!preset || presetBusy) return;
  setPresetBusy(true);
  let wasPaused;
  try {
    await settleControls();
    wasPaused = (await request('state')).paused;
    await request('pause', {});
    const saved = preset.settings;
    const fluctuations = Object.fromEntries(Object.keys(metadata.timescales).map(key => [key, null]));
    Object.assign(fluctuations, saved.fluctuations);
    await request('controls', { ...saved.controls, fluctuations, outputs: saved.outputs, position: saved.position });
    const restored = await request(saved.paused ? 'pause' : 'resume', {});
    routeDirty = false;
    fitted = false;
    render(restored);
    error('');
  } catch (failure) {
    if (wasPaused !== undefined) await command(wasPaused ? 'pause' : 'resume');
    error(failure.message);
    refreshPresets();
  } finally { setPresetBusy(false); }
};
byId('delete-preset').onclick = () => {
  const name = byId('preset-select').value;
  if (!name || !window.confirm(`Delete preset "${name}"?`)) return;
  try { persistPresets(presets.filter(preset => preset.name !== name), ''); error(''); }
  catch (failure) { error(failure.message); }
};

function fitRoute() {
  const anchored = state?.controls.mode === 'anchor';
  map.fitBounds(anchored ? anchorZone.getBounds() : route.getBounds(),
    { padding: [45, 45], maxZoom: anchored ? 18 : 14, animate: false });
}

new ResizeObserver(() => {
  map.invalidateSize({ pan: false });
  if (fitted) fitRoute();
}).observe(byId('chart'));

function render(next) {
  if (state?.controls.mode !== next.controls.mode) fitted = false;
  state = next;
  byId('connection').textContent = next.paused ? 'Paused' : 'Live';
  byId('connection-dot').classList.add('online');
  for (const button of document.querySelectorAll('[data-mode]')) {
    const selected = button.dataset.mode === next.controls.mode;
    button.classList.toggle('active', selected);
    button.setAttribute('aria-pressed', selected);
  }
  byId('engineRunning').checked = next.controls.engineRunning;
  byId('engine-state').textContent = next.controls.engineRunning ? 'Running' : 'Stopped';
  for (const [, key, , unit, , , step, factor = 1] of controls) {
    const bounds = next.fluctuations?.[key];
    const reported = { sailingSpeed: next.speedThroughWater, engineRpm: next.engineRpm,
      oilPressure: next.oilPressure, alternatorVoltage: next.alternatorVoltage,
      coolantTemperature: next.coolantTemperature - 273.15 };
    const live = reported[key] ?? next.effectiveControls?.[key] ?? next.controls[key];
    byId(`${key}-live`).textContent = `${(live / factor).toFixed(step < 1 ? 1 : 0)} ${unit}`;
    const coupled = key === 'sailingSpeed' && next.controls.engineRunning;
    const slider = rangeSliders.get(key);
    if (coupled) slider.disable();
    else slider.enable();
    byId(`${key}-range`).title = coupled ? 'Speed follows engine RPM; adjust the engine speed range' : '';
    for (const limit of ['min', 'max']) byId(`${key}-${limit}`).disabled = coupled;
    if (!pending.has(key)) byId(`${key}-mode`).textContent = coupled ? 'RPM coupled' : bounds && bounds.min !== bounds.max ? 'Variable' : 'Fixed';
    if (pending.has(key) || editingRanges.has(key)) continue;
    const values = coupled ? [live / factor, live / factor]
      : bounds ? [bounds.min / factor, bounds.max / factor] : [next.controls[key] / factor, next.controls[key] / factor];
    if (![byId(`${key}-min`), byId(`${key}-max`)].includes(document.activeElement)) {
      const current = slider.get().map(Number);
      if (values.some((value, index) => Math.abs(value - current[index]) > 0.0001)) slider.set(values);
      byId(`${key}-min`).value = Number(values[0].toFixed(4));
      byId(`${key}-max`).value = Number(values[1].toFixed(4));
    }
  }
  if (!routeDirty) for (const key of routeKeys) byId(key).value = next.controls[key].toFixed(4);
  byId('sog').textContent = (next.speedOverGround * 1.943844).toFixed(1);
  byId('heading').textContent = Math.round(next.heading * 180 / Math.PI) % 360;
  const marina = next.controls.mode === 'marina';
  const anchored = next.controls.mode === 'anchor';
  byId('heading').hidden = marina;
  byId('marinaHeading').hidden = !marina;
  if (document.activeElement !== byId('marinaHeading')) byId('marinaHeading').value = next.controls.marinaHeading;
  byId('anchor-controls').hidden = !anchored;
  if (document.activeElement !== byId('chainLength')) byId('chainLength').value = next.controls.chainLength;
  byId('anchor-radius').textContent = `Swing radius ${(next.anchorSwingRadius ?? 0).toFixed(1)} m`;
  byId('chain-warning').hidden = !next.anchorChainTooShort;
  byId('wind').textContent = (next.apparentWindSpeed * 1.943844).toFixed(1);
  byId('depth-reading').textContent = next.depth.toFixed(1);
  byId('frames').textContent = next.frames.toLocaleString();
  byId('clients').textContent = next.clients;
  for (const [path, inputs] of outputSwitches) for (const input of inputs) {
    input.checked = next.outputs?.[path] !== false;
    input.parentElement.classList.toggle('not-transmitting', !input.checked);
  }
  for (const [path, output] of derivedOutputs) output.textContent = derivedReadings[path](next);
  byId('endpoint').textContent = `127.0.0.1:${next.n2kPort}`;
  byId('output-mode').textContent = next.output === 'signalk' ? 'Signal K deltas + TCP' : 'N2K TCP';
  byId('footer-status').textContent = next.output === 'signalk' ? 'Signal K plugin / direct injection' : `Local N2K feed / source ${next.sourceAddress}`;
  const seconds = Math.floor(next.elapsed);
  byId('elapsed').textContent = `${String(Math.floor(seconds / 60)).padStart(2, '0')}:${String(seconds % 60).padStart(2, '0')}`;
  byId('position').textContent = `${next.latitude.toFixed(5)}, ${next.longitude.toFixed(5)}`;
  byId('trip-state').textContent = next.controls.mode === 'marina' ? 'Moored' : next.controls.mode === 'anchor' ? 'At anchor' : next.arrived ? 'Arrived' : 'On passage';
  byId('anchor-distance').hidden = !anchored;
  byId('anchor-distance').textContent = `Anchor distance ${(next.anchorDistance ?? 0).toFixed(1)} m`;
  const start = [next.controls.departureLatitude, next.controls.departureLongitude];
  const end = [next.controls.destinationLatitude, next.controls.destinationLongitude];
  departure.setLatLng(start);
  departure.setTooltipContent(anchored ? 'Anchor' : 'Departure');
  byId('departure-label').textContent = anchored ? 'Anchor' : 'Departure';
  byId('pick-departure').title = anchored ? 'Set anchor on chart' : 'Set departure on chart';
  byId('pick-departure').setAttribute('aria-label', byId('pick-departure').title);
  anchorZone.setLatLng(start).setRadius(next.anchorSwingRadius ?? 0);
  if (anchored && !map.hasLayer(anchorZone)) anchorZone.addTo(map);
  else if (!anchored && map.hasLayer(anchorZone)) anchorZone.remove();
  if (!draggingDestination) destination.setLatLng(end);
  route.setLatLngs([[next.latitude, next.longitude], anchored ? start : end]);
  if (!draggingBoat) boat.setLatLng([next.latitude, next.longitude]);
  const marker = boat.getElement()?.querySelector('svg');
  if (marker) marker.style.transform = `rotate(${next.heading * 180 / Math.PI - 45}deg)`;
  if (!fitted) { fitRoute(); fitted = true; }
  byId('pause').innerHTML = `<i data-lucide="${next.paused ? 'play' : 'pause'}"></i>`;
  byId('pause').title = next.paused ? 'Resume simulation' : 'Pause simulation';
  byId('pause').setAttribute('aria-label', byId('pause').title);
  lucide.createIcons();
}

function selectTab(name) {
  for (const tab of document.querySelectorAll('[data-tab]')) {
    const selected = tab.dataset.tab === name;
    tab.setAttribute('aria-selected', selected);
    tab.tabIndex = selected ? 0 : -1;
    byId(tab.getAttribute('aria-controls')).hidden = !selected;
  }
}

const tabs = [...document.querySelectorAll('[data-tab]')];
for (const tab of tabs) {
  tab.onclick = () => selectTab(tab.dataset.tab);
  tab.onkeydown = event => {
    const index = tabs.indexOf(tab);
    const target = { ArrowRight: (index + 1) % tabs.length, ArrowLeft: (index + tabs.length - 1) % tabs.length,
      Home: 0, End: tabs.length - 1 }[event.key];
    if (target === undefined) return;
    event.preventDefault();
    selectTab(tabs[target].dataset.tab);
    tabs[target].focus();
  };
}

for (const button of document.querySelectorAll('[data-mode]')) button.onclick = () => command('controls', { mode: button.dataset.mode });
byId('engineRunning').onchange = event => command('controls', { engineRunning: event.target.checked });
byId('marinaHeading').onchange = event => {
  const input = event.target;
  if (input.value === '' || !input.reportValidity()) return;
  command('controls', { marinaHeading: Number(input.value) });
};
byId('chainLength').onchange = event => {
  const input = event.target;
  if (input.value === '' || !input.reportValidity()) return;
  command('controls', { chainLength: Number(input.value) });
};
byId('pause').onclick = () => state && command(state.paused ? 'resume' : 'pause');
byId('reset').onclick = () => command('reset');
byId('fit').onclick = fitRoute;
for (const [id, enabled] of [['enable-all', true], ['disable-all', false]]) byId(id).onclick = () => {
  if (metadata) command('controls', { outputs: Object.fromEntries(metadata.readings.map(([path]) => [path, enabled])) });
};
for (const key of routeKeys) byId(key).addEventListener('input', () => { routeDirty = true; });
byId('apply-route').onclick = async () => {
  const patch = {};
  for (const key of routeKeys) {
    if (!byId(key).checkValidity() || byId(key).value === '') { byId(key).reportValidity(); return; }
    patch[key] = Number(byId(key).value);
  }
  if (await command('controls', patch)) { routeDirty = false; fitted = false; }
};
for (const target of ['departure', 'destination']) byId(`pick-${target}`).onclick = () => {
  selection = selection === target ? null : target;
  for (const name of ['departure', 'destination']) byId(`pick-${name}`).classList.toggle('selected', selection === name);
  map.getContainer().style.cursor = selection ? 'crosshair' : '';
};
map.on('click', event => {
  if (!selection) return;
  byId(`${selection}Latitude`).value = event.latlng.lat.toFixed(4);
  byId(`${selection}Longitude`).value = event.latlng.lng.toFixed(4);
  routeDirty = true;
  selection = null;
  for (const name of ['departure', 'destination']) byId(`pick-${name}`).classList.remove('selected');
  map.getContainer().style.cursor = '';
});

async function poll() {
  try { render(await request('state')); }
  catch (failure) {
    byId('connection').textContent = 'Disconnected';
    byId('connection-dot').classList.remove('online');
    error(failure.message);
  }
  setTimeout(poll, 500);
}
async function initialize() {
  try {
    const stored = JSON.parse(localStorage.getItem(presetStorageKey) || '{"version":1,"presets":[]}');
    if (stored.version !== 1 || !Array.isArray(stored.presets) || stored.presets.some(preset =>
      typeof preset.name !== 'string' || !preset.name.trim() || !preset.settings?.controls ||
      !preset.settings.fluctuations || !preset.settings.outputs || !preset.settings.position || typeof preset.settings.paused !== 'boolean')) {
      throw new Error('Saved presets are invalid.');
    }
    presets = stored.presets;
    refreshPresets();
  } catch (failure) { error(`Unable to read presets: ${failure.message}`); }
  try {
    metadata = await request('parameters');
    for (const [path, label] of metadata.readings) {
      let containers = [...document.querySelectorAll(`[data-output="${path}"]`)];
      if (!containers.length) {
        const row = document.createElement('div');
        row.className = 'derived-row';
        const text = document.createElement('span');
        text.textContent = label;
        const value = document.createElement('div');
        value.className = 'reading-value';
        value.dataset.output = path;
        const output = document.createElement('output');
        value.append(output);
        row.append(text, value);
        byId('derived-readings').append(row);
        derivedOutputs.set(path, output);
        containers = [value];
      }
      const inputs = containers.map(container => {
        const input = document.createElement('input');
        input.type = 'checkbox';
        input.setAttribute('role', 'switch');
        input.setAttribute('aria-label', `Transmit ${label}`);
        input.title = `Transmit ${label}`;
        input.onchange = () => command('controls', { outputs: { [path]: input.checked } });
        container.append(input);
        return input;
      });
      outputSwitches.set(path, inputs);
    }
    for (const [, key, label] of controls) {
      const interval = metadata.timescales[key];
      byId(`${key}-mode`).title = `${label}: equal limits hold steady; a range varies smoothly over ${interval[0]}-${interval[1]} seconds`;
    }
  } catch (failure) { error(failure.message); }
  poll();
}
initialize();