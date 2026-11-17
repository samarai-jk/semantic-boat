'use strict';

// Diagnostic Sessions: the recordings Semantic Boat Devices fetched from
// B1S devices (src/lib/b1s/DIAGNOSTICS.md). Generic for every kind: any
// session shows its series as stacked graphs grouped by unit, its switch
// inputs as bars and its phases as bands; kinds with key numbers (engine
// start) show them above.
(() => {
  const api = path => fetch(`/plugins/semantic-boat-devices/diagnostics/${path}`, { credentials: 'include' })
    .then(async response => {
      const data = await response.json().catch(() => ({}));
      if (!response.ok) throw new Error(data.error || `${response.status} ${response.statusText}`);
      return data;
    });
  const element = (tag, attributes = {}, ...children) => {
    const node = document.createElement(tag);
    for (const [key, value] of Object.entries(attributes)) {
      if (key === 'class') node.className = value;
      else if (key.startsWith('on')) node.addEventListener(key.slice(2), value);
      else node.setAttribute(key, value);
    }
    for (const child of children.flat()) if (child !== null && child !== undefined) node.append(child);
    return node;
  };
  const svg = (tag, attributes = {}) => {
    const node = document.createElementNS('http://www.w3.org/2000/svg', tag);
    for (const [key, value] of Object.entries(attributes)) node.setAttribute(key, value);
    return node;
  };
  const words = id => String(id ?? '').replace(/([a-z])([A-Z])/g, '$1 $2').replace(/^./, first => first.toUpperCase());
  const kindNames = { engineStart: 'Engine start' };
  const roleNames = { batteryVoltage: 'Battery', starterVoltage: 'Starter', alternatorExcitation: 'Alternator D+',
    oilPressureSender: 'Oil pressure sender', engineSpeed: 'Engine speed' };
  const inputNames = { gearReverse: 'Reverse', gearForward: 'Forward', fuelStop: 'Fuel stop' };
  const phaseNames = { waiting: 'Waiting', waitingLong: 'Waiting', cranking: 'Cranking', runningUp: 'Running up' };
  const outcomeClass = outcome => (outcome === 'started' || outcome === 'finished' ? 'good'
    : outcome === 'no start' || outcome === 'aborted' ? 'bad' : 'other');
  const when = iso => (iso ? new Date(iso).toLocaleString(undefined, { dateStyle: 'medium', timeStyle: 'medium' }) : 'time unknown');
  const fixed = (value, digits) => (value === null || value === undefined ? '–' : Number(value).toFixed(digits));

  // A series in the unit people read: rpm instead of Hz, °C instead of K.
  function displayOf(series) {
    if (series.unit === 'Hz' && (series.role === 'engineSpeed' || series.quantity === 'rotationalSpeed')) {
      return { unit: 'rpm', convert: value => value * 60, digits: 0 };
    }
    if (series.unit === 'K') return { unit: '°C', convert: value => value - 273.15, digits: 1 };
    if (series.unit === 'ratio') return { unit: '%', convert: value => value * 100, digits: 0 };
    return { unit: series.unit ?? '', convert: value => value, digits: series.unit === 'V' ? 2 : 1 };
  }
  const seriesName = series => series.label || roleNames[series.role] || words(series.role) || `Channel ${series.channel}`;

  const state = { sessions: [], selected: null, session: null, view: null, kind: '' };
  const list = document.getElementById('sessions');
  const listState = document.getElementById('list-state');
  const detail = document.getElementById('detail');
  const kindFilter = document.getElementById('kind-filter');

  async function loadList() {
    try {
      state.sessions = await api('sessions');
      const kinds = [...new Set(state.sessions.map(session => session.kind))];
      kindFilter.replaceChildren(element('option', { value: '' }, 'All kinds'),
        ...kinds.map(kind => element('option', { value: kind }, kindNames[kind] ?? words(kind))));
      kindFilter.value = kinds.includes(state.kind) ? state.kind : '';
      renderList();
      if (!state.selected && state.sessions.length) select(state.sessions[0].name);
    } catch (error) {
      listState.textContent = `Sessions could not be loaded: ${error.message}. Log in to Signal K as an administrator.`;
    }
  }

  function renderList() {
    const shown = state.sessions.filter(session => !state.kind || session.kind === state.kind);
    listState.textContent = shown.length ? `${shown.length} session${shown.length === 1 ? '' : 's'}`
      : 'No sessions yet. They appear here a few seconds after a device has recorded one.';
    list.replaceChildren(...shown.map(session => {
      const outcome = session.summary?.outcome ?? session.endReason;
      const item = element('li', { class: `session${session.name === state.selected ? ' selected' : ''}`, tabindex: '0',
        onclick: () => select(session.name), onkeydown: event => { if (event.key === 'Enter') select(session.name); } },
      element('div', { class: 'row' }, element('span', { class: 'when' }, when(session.startedAt)),
        element('span', { class: `chip ${outcomeClass(outcome)}` }, outcome)),
      element('div', { class: 'meta' }, `${kindNames[session.kind] ?? words(session.kind)} · ${session.device} · ` +
        `${fixed(session.durationMs / 1000, 1)} s${session.truncated ? ' · truncated' : ''}`));
      return item;
    }));
  }

  async function select(name) {
    state.selected = name;
    renderList();
    try {
      state.session = await api(`sessions/${encodeURIComponent(name)}`);
      state.view = [Math.min(0, ...state.session.series.map(series => series.t[0] ?? 0)), state.session.durationMs];
      renderDetail();
    } catch (error) {
      detail.replaceChildren(element('div', { class: 'empty' }, element('h2', {}, 'Session not loaded'), element('p', {}, error.message)));
    }
  }

  // --- key numbers -----------------------------------------------------------

  function figure(label, value, unit = '') {
    return element('div', { class: 'figure' }, element('span', { class: 'label' }, label),
      element('span', { class: 'value' }, value, unit ? element('small', {}, unit) : null));
  }

  function engineStartFigures(summary) {
    const blocks = [element('div', { class: 'figures' },
      figure('Battery at rest', fixed(summary.restingV, 2), 'V'),
      figure('Preheat', fixed(summary.preheatS, 1), 's'),
      figure('Voltage drop while preheating', fixed(summary.preheatDropV, 2), 'V'),
      figure('Crank attempts', String(summary.crankAttempts)),
      figure('First crank to running', fixed(summary.timeToRunS, 1), 's'),
      figure('Charging after', fixed(summary.chargingAfterS, 1), 's'),
      figure('Idle speed', summary.idleRpm === null ? '–' : String(summary.idleRpm), 'rpm'))];
    if (summary.cranks?.length) {
      blocks.push(element('div', { class: 'table-wrap' }, element('table', { class: 'cranks' },
        element('thead', {}, element('tr', {}, ...['Crank', 'Duration s', 'Lowest battery V', 'Lowest starter V',
          'Cable loss V', 'Compression pulses /s'].map(text => element('th', {}, text)))),
        element('tbody', {}, ...summary.cranks.map((crank, index) => element('tr', {},
          element('td', {}, element('button', { type: 'button', onclick: () => zoom(crank.startMs - 500, crank.startMs + crank.durationMs + 1000) },
            `${index + 1}`)),
          element('td', {}, fixed(crank.durationMs / 1000, 2)), element('td', {}, fixed(crank.minBatteryV, 2)),
          element('td', {}, fixed(crank.minStarterV, 2)), element('td', {}, fixed(crank.maxCableLossV, 2)),
          element('td', {}, fixed(crank.compressionPulsesPerS, 1))))))));
    }
    return blocks;
  }

  function renderDetail() {
    const session = state.session;
    const outcome = session.summary?.outcome ?? session.endReason;
    const parts = [element('div', { class: 'card title' },
      element('div', {}, element('h2', {}, `${kindNames[session.kind] ?? words(session.kind)}, ${when(session.startedAt)}`),
        element('p', {}, `${session.device} · session ${session.number} · ${fixed(session.durationMs / 1000, 1)} s` +
          (session.truncated ? ' · truncated: the device ran out of memory' : ''))),
      element('span', { class: `chip ${outcomeClass(outcome)}` }, outcome))];
    if (session.kind === 'engineStart' && session.summary) {
      parts.push(element('div', { class: 'card' }, element('h3', {}, 'Key numbers'), ...engineStartFigures(session.summary)));
    }
    const snapshots = session.events.filter(event => event.snapshot);
    if (snapshots.length) {
      parts.push(element('div', { class: 'card' }, element('h3', {}, 'At the start'),
        element('ul', { class: 'snapshots' }, ...snapshots.map(event => {
          const shown = event.value === null ? null
            : event.unit === 'K' ? `${fixed(event.value - 273.15, 1)} °C`
            : event.quantity === 'runTime' ? `${fixed(event.value / 3600, 1)} h`
            : event.unit === 'V' ? `${fixed(event.value, 2)} V` : `${event.value} ${event.unit ?? ''}`;
          return element('li', {}, `${event.label || `Channel ${event.channel}`}: `, element('b', {}, shown ?? '–'));
        }))));
    }
    const chartCard = element('div', { class: 'card' });
    parts.push(chartCard);
    detail.replaceChildren(...parts);
    renderChart(chartCard);
  }

  function zoom(from, to) {
    state.view = [from, Math.max(to, from + 50)];
    const card = detail.querySelector('.chart')?.parentElement;
    if (card) renderChart(card);
  }

  // --- the graphs ----------------------------------------------------------------

  function renderChart(card) {
    const session = state.session;
    const [from, to] = state.view;
    const width = 900; const left = 64; const right = 16; const plotWidth = width - left - right;
    const x = t => left + (t - from) / (to - from) * plotWidth;
    const timeOf = px => from + (px - left) / plotWidth * (to - from);
    // Lanes: series grouped by display unit; then the switch inputs.
    const lanes = [];
    session.series.forEach((series, index) => {
      if (!series.v.some(value => value !== null)) return;
      const display = displayOf(series);
      let lane = lanes.find(entry => entry.unit === display.unit);
      if (!lane) { lane = { unit: display.unit, series: [] }; lanes.push(lane); }
      lane.series.push({ series, display, color: `var(--trace-${(index % 4) + 1})` });
    });
    const inputs = [...new Map(session.events.filter(event => !event.snapshot && event.on !== undefined)
      .map(event => [event.channel, event])).values()];
    const laneHeight = 150; const gap = 26; const inputRow = 18;
    const inputsHeight = inputs.length ? inputs.length * inputRow + 20 : 0;
    const height = 24 + lanes.length * (laneHeight + gap) + inputsHeight + 24;
    const chart = svg('svg', { viewBox: `0 0 ${width} ${height}`, role: 'img', 'aria-label': 'Graphs of the session' });
    // Phase bands across everything.
    const phases = session.phases.map((phase, index) => ({ ...phase, end: session.phases[index + 1]?.t ?? session.durationMs }));
    for (const phase of phases) {
      const x0 = Math.max(left, x(phase.t)); const x1 = Math.min(width - right, x(phase.end));
      if (x1 <= x0) continue;
      chart.append(svg('rect', { x: x0, y: 16, width: x1 - x0, height: height - 40, class: `band-${phase.phase}` }));
      if (x1 - x0 > 60) {
        const label = svg('text', { x: x0 + 4, y: 12 }); label.textContent = phaseNames[phase.phase] ?? words(phase.phase);
        chart.append(label);
      }
    }
    // Time axis.
    const span = to - from;
    const step = [50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 30000, 60000].find(candidate => span / candidate <= 10) ?? 120000;
    const axisY = height - 18;
    for (let t = Math.ceil(from / step) * step; t <= to; t += step) {
      chart.append(svg('line', { x1: x(t), x2: x(t), y1: 16, y2: axisY - 4, class: 'grid' }));
      const label = svg('text', { x: x(t), y: axisY + 10, 'text-anchor': 'middle' });
      label.textContent = `${(t / 1000).toFixed(step < 1000 ? 2 : 0)} s`;
      chart.append(label);
    }
    let top = 24;
    const laneGeometry = [];
    for (const lane of lanes) {
      let min = Infinity; let max = -Infinity;
      for (const { series, display } of lane.series) {
        series.v.forEach((value, index) => {
          if (value === null || series.t[index] < from || series.t[index] > to) return;
          const shown = display.convert(value); min = Math.min(min, shown); max = Math.max(max, shown);
        });
      }
      if (!Number.isFinite(min)) { min = 0; max = 1; }
      if (max - min < 0.5) { max += 0.25; min -= 0.25; }
      const pad = (max - min) * 0.08; min -= pad; max += pad;
      const y = value => top + laneHeight - (value - min) / (max - min) * laneHeight;
      chart.append(svg('line', { x1: left, x2: width - right, y1: top + laneHeight, y2: top + laneHeight, class: 'axis' }));
      for (const fraction of [0, 0.5, 1]) {
        const value = min + (max - min) * fraction;
        const label = svg('text', { x: left - 6, y: y(value) + 4, 'text-anchor': 'end' });
        label.textContent = value.toFixed(Math.abs(max - min) < 5 ? 1 : 0);
        chart.append(label);
      }
      const title = svg('text', { x: left - 6, y: top - 6, 'text-anchor': 'end', class: 'lane-title' });
      title.textContent = lane.unit;
      chart.append(title);
      for (const { series, display, color } of lane.series) {
        let path = ''; let pen = false;
        series.v.forEach((value, index) => {
          const t = series.t[index];
          if (value === null || t < from - span * 0.02 || t > to + span * 0.02) { pen = false; return; }
          path += `${pen ? 'L' : 'M'}${x(t).toFixed(1)} ${y(display.convert(value)).toFixed(1)}`;
          pen = true;
        });
        chart.append(svg('path', { d: path, fill: 'none', stroke: color, 'stroke-width': 1.6, 'stroke-linejoin': 'round' }));
      }
      laneGeometry.push({ top, y, lane });
      top += laneHeight + gap;
    }
    // Switch inputs as bars while on.
    inputs.forEach((input, row) => {
      const rowTop = top + row * inputRow;
      const label = svg('text', { x: left - 6, y: rowTop + 12, 'text-anchor': 'end' });
      label.textContent = inputNames[input.input] || words(input.input) || input.label || `Ch ${input.channel}`;
      chart.append(label);
      chart.append(svg('line', { x1: left, x2: width - right, y1: rowTop + 14, y2: rowTop + 14, class: 'grid' }));
      const changes = session.events.filter(event => event.channel === input.channel && event.on !== undefined);
      changes.forEach((change, index) => {
        if (!change.on) return;
        const end = changes.slice(index + 1).find(next => !next.on)?.t ?? session.durationMs;
        const x0 = Math.max(left, x(change.t)); const x1 = Math.min(width - right, x(end));
        if (x1 > x0) chart.append(svg('rect', { x: x0, y: rowTop + 3, width: Math.max(1.5, x1 - x0), height: 10, rx: 2, class: 'input-on' }));
      });
    });
    // Cursor, readout and drag-to-zoom.
    const cursor = svg('line', { y1: 16, y2: axisY - 4, class: 'cursor', visibility: 'hidden' });
    const selection = svg('rect', { y: 16, height: axisY - 20, class: 'selection', visibility: 'hidden' });
    chart.append(selection, cursor);
    const readout = element('div', { class: 'readout' }, 'Point at the graphs for the values; drag to zoom in.');
    const pointX = event => {
      const box = chart.getBoundingClientRect();
      return (event.clientX - box.left) / box.width * width;
    };
    let dragFrom = null;
    chart.addEventListener('pointerdown', event => { dragFrom = pointX(event); chart.setPointerCapture(event.pointerId); });
    chart.addEventListener('pointermove', event => {
      const px = Math.min(width - right, Math.max(left, pointX(event)));
      cursor.setAttribute('x1', px); cursor.setAttribute('x2', px); cursor.setAttribute('visibility', 'visible');
      if (dragFrom !== null) {
        selection.setAttribute('x', Math.min(dragFrom, px)); selection.setAttribute('width', Math.abs(px - dragFrom));
        selection.setAttribute('visibility', 'visible');
      }
      const t = timeOf(px);
      const values = session.series.filter(series => series.t.length).map(series => {
        let best = 0;
        for (let index = 1; index < series.t.length; index += 1) {
          if (Math.abs(series.t[index] - t) < Math.abs(series.t[best] - t)) best = index;
        }
        const display = displayOf(series);
        const value = series.v[best];
        return `${seriesName(series)} ${value === null ? '–' : display.convert(value).toFixed(display.digits)} ${display.unit}`;
      });
      readout.textContent = `${(t / 1000).toFixed(2)} s · ${values.join(' · ')}`;
    });
    chart.addEventListener('pointerup', event => {
      const px = pointX(event);
      selection.setAttribute('visibility', 'hidden');
      if (dragFrom !== null && Math.abs(px - dragFrom) > 8) zoom(timeOf(Math.min(px, dragFrom)), timeOf(Math.max(px, dragFrom)));
      dragFrom = null;
    });
    chart.addEventListener('pointerleave', () => cursor.setAttribute('visibility', 'hidden'));
    // Tools: whole session and each crank; legend.
    const whole = [Math.min(0, ...session.series.map(series => series.t[0] ?? 0)), session.durationMs];
    const crankButtons = (session.summary?.cranks ?? []).map((crank, index) => element('button', { type: 'button',
      onclick: () => zoom(crank.startMs - 500, crank.startMs + crank.durationMs + 1000) }, `Crank ${index + 1}`));
    const legend = element('div', { class: 'legend' }, ...lanes.flatMap(lane => lane.series.map(({ series, color }) =>
      element('span', {}, element('i', { style: `background:${color}` }), seriesName(series)))));
    card.replaceChildren(
      element('div', { class: 'chart-tools' }, element('h3', {}, 'Graphs'),
        element('div', { class: 'buttons' },
          element('button', { type: 'button', class: from === whole[0] && to === whole[1] ? 'active' : '',
            onclick: () => zoom(whole[0], whole[1]) }, 'Whole session'), ...crankButtons)),
      legend, element('div', { class: 'chart' }, chart), readout);
  }

  kindFilter.addEventListener('change', () => { state.kind = kindFilter.value; renderList(); });
  document.getElementById('refresh').addEventListener('click', loadList);
  loadList();
  // New sessions arrive a few seconds after a device has recorded them.
  setInterval(loadList, 30000);
})();
