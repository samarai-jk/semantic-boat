'use strict';

// Devices view: every device of every transport in one list (left), the
// selected device in detail (right). Displays (any transport) show their
// live bindings and install configurations from the designer's catalog; B1S
// devices show channels, parameters, calibration, their log, and backups
// (src/lib/b1s/CONFIGURATION.md). Uses api(), notice() and text() of app.js.
(() => {
  const list = document.getElementById('device-list');
  const detail = document.getElementById('device-detail');
  const view = document.getElementById('devices-view');
  const encodings = ['automatic', 'standard', 'b1s', 'off'];
  const kinds = { display: 'Display', b1s: 'B1S device' };
  let devices = [];
  let catalogNames = [];
  let selected = decodeURIComponent(window.location.hash.slice(1)) || null; // "transport/id"
  let rendered = { key: null, version: 0 };
  const b1sCards = new Map(); // device ID -> { parameters, version, log state }

  const keyOf = device => `${device.transport}/${device.id}`;

  function button(label, action, title = '') {
    const element = text('button', label, 'small-button');
    if (title) element.title = title;
    element.onclick = async () => {
      element.disabled = true;
      try { await action(); } catch (error) { notice(error.message, true); } finally { element.disabled = false; }
    };
    return element;
  }

  function table(headings, rows, className = '') {
    const scroll = text('div', '', 'table-scroll');
    const element = document.createElement('table');
    const head = document.createElement('tr');
    for (const heading of headings) head.append(text('th', heading));
    const body = document.createElement('tbody');
    if (className) body.className = className;
    for (const row of rows) body.append(row);
    element.append(head, body);
    scroll.append(element);
    return scroll;
  }

  function cells(...values) {
    const row = document.createElement('tr');
    for (const value of values) {
      if (value instanceof Node) { const cell = document.createElement('td'); cell.append(value); row.append(cell); }
      else row.append(text('td', value ?? ''));
    }
    return row;
  }

  // SI value <-> what people read: °C for absolute temperatures, % for ratios.
  function display(si, unit, absolute) {
    if (si === null || si === undefined) return { text: '–', value: '' };
    if (unit === 'K' && absolute) return { text: `${(si - 273.15).toFixed(2)} °C`, value: (si - 273.15).toFixed(3), unit: '°C' };
    if (unit === 'ratio') return { text: `${(si * 100).toFixed(2)} %`, value: (si * 100).toFixed(4), unit: '%' };
    const value = Number(si.toPrecision(7));
    return { text: `${value} ${unit ?? ''}`.trim(), value: String(value), unit: unit ?? '' };
  }
  function fromDisplay(input, unit, absolute) {
    const number = Number(input);
    if (!Number.isFinite(number)) throw new Error(`Not a number: ${input}`);
    if (unit === 'K' && absolute) return number + 273.15;
    if (unit === 'ratio') return number / 100;
    return number;
  }

  // --- list -----------------------------------------------------------------

  function renderList() {
    list.replaceChildren();
    if (!devices.length) list.append(text('p', 'No devices. Enable B1S or the RS485 plugin.', 'empty'));
    const transports = [...new Set(devices.map(device => device.transportLabel))];
    for (const transport of transports) {
      list.append(text('h3', transport, 'transport-heading'));
      for (const device of devices.filter(entry => entry.transportLabel === transport)) {
        const item = text('button', '', `device-item${keyOf(device) === selected ? ' selected' : ''}`);
        item.append(text('span', '', `dot ${device.online ? 'online' : 'offline'}`), text('span', device.label, 'device-name'),
          text('small', `${kinds[device.kind] ?? device.kind} · ${device.status}`));
        item.onclick = () => {
          selected = keyOf(device);
          window.location.hash = encodeURIComponent(selected);
          rendered.key = null;
          render();
        };
        list.append(item);
      }
    }
    const online = devices.filter(device => device.online).length;
    document.getElementById('connection').textContent = `${online}/${devices.length} device(s) online`;
  }

  // --- displays -------------------------------------------------------------

  function renderDisplay(device) {
    const info = device.detail ?? {};
    const header = text('div', '', 'device-header');
    const title = text('div');
    title.append(text('h2', device.label), text('span', device.status, `badge ${device.online ? '' : 'offline'}`));
    header.append(title);
    if (device.canInstall) {
      const controls = text('div', '', 'install-controls');
      const select = document.createElement('select');
      select.setAttribute('aria-label', 'Configuration to install');
      for (const name of catalogNames) select.append(Object.assign(text('option', name), { value: name }));
      if (!catalogNames.length) select.append(text('option', 'No configurations'));
      controls.append(select, button('Install', async () => {
        if (!confirm(`Install ${select.value} on ${device.label}?`)) return;
        const result = await api(`devices/${encodeURIComponent(device.transport)}/${encodeURIComponent(device.id)}/install`,
          'POST', { name: select.value });
        notice(result.status);
      }, 'Send the configuration from the designer catalog to this display'));
      header.append(controls);
    }
    const meta = text('p', `${device.transportLabel} · ${device.address ?? ''} · ${device.id}`, 'device-meta');
    const rows = (info.subscriptions ?? []).map(source => {
      const row = cells(String(source.sourceIndex), source.path + (source.path !== source.resolvedPath ? ` → ${source.resolvedPath}` : ''),
        source.value === null || source.value === undefined ? 'Unavailable' : String(source.value), source.source ?? '');
      row.children[1].className = 'path';
      row.children[2].className = 'value';
      return row;
    });
    const stats = text('div', [`${info.receivedFrames ?? 0} RX`, `${info.sentFrames ?? 0} TX`, `${info.valuesSent ?? 0} values`,
      `${info.decodeErrors ?? 0} decode errors`, `${info.queuedFrames ?? 0} queued`].join(' · '), 'stats');
    const transfer = text('div', info.lastTransfer ? `Last install: ${info.lastTransfer.name}: ${info.lastTransfer.state}` : '', 'transfer');
    detail.replaceChildren(header, meta, text('h3', 'Live values requested by the display'),
      rows.length ? table(['#', 'Signal K path', 'Value (SI)', 'Source'], rows, 'subscriptions') : text('p', 'No subscriptions yet', 'empty'),
      stats, text('div', info.error ?? '', 'error-text'), transfer);
  }

  function updateDisplay(device) {
    const body = detail.querySelector('tbody.subscriptions');
    (device.detail?.subscriptions ?? []).forEach((source, index) => {
      const row = body?.children[index];
      if (row) {
        row.children[2].textContent = source.value === null || source.value === undefined ? 'Unavailable' : String(source.value);
        row.children[3].textContent = source.source ?? '';
      }
    });
  }

  // --- B1S devices ------------------------------------------------------------

  function b1sCard(id) {
    if (!b1sCards.has(id)) {
      b1sCards.set(id, { parameters: null, version: 0, logOpen: false, logSince: 0, logText: '', logRemaining: 0,
        levels: new Set(Object.keys(levels)), openChannels: new Set(), channelTab: new Map() });
    }
    return b1sCards.get(id);
  }

  async function loadParameters(id) {
    const card = b1sCard(id);
    if (card.loading) return;
    card.loading = true;
    try {
      card.parameters = (await api(`b1s/devices/${id}/parameters`)).parameters;
      card.version += 1;
    } finally { card.loading = false; }
  }

  // --- B1S device: settings grouped by device and channel --------------------

  const severityLabels = { off: 'Off', warning: 'Warning', alarm: 'Alarm', emergency: 'Emergency' };
  const functionLabels = { none: 'latest value', last: 'latest value', average: 'average', minimum: 'minimum',
    maximum: 'maximum', change: 'change', rate: 'rate of change' };
  const comparisonLabels = { above: 'above', atLeast: 'at least', below: 'below', atMost: 'at most',
    inside: 'inside', outside: 'outside' };
  const windowed = name => ['average', 'minimum', 'maximum', 'change'].includes(name);
  const encodingLabels = { automatic: 'automatic', standard: 'standard PGN', b1s: 'B1S message', off: 'off (not sent)' };

  function age(ms) {
    if (ms === null || ms === undefined) return '';
    const seconds = Math.round(ms / 1000);
    if (seconds < 60) return `${seconds} s ago`;
    if (seconds < 3600) return `${Math.floor(seconds / 60)} min ago`;
    return `${Math.floor(seconds / 3600)} h ago`;
  }

  // What the channel header shows: value (or off), how it is sent, age, path.
  function channelState(channel) {
    const via = channel.encoding === 'standard' ? `standard PGN ${channel.standardPgn ?? ''}, instance ${channel.standardInstance}`
      : channel.encoding === 'off' ? 'off: not sent' : 'B1S message';
    const value = channel.encoding === 'off' ? 'off' : display(channel.value, channel.unit, true).text;
    const stale = channel.ageMs !== null && channel.period && channel.ageMs > Math.max(3 * channel.period * 1000, 10000);
    const path = channel.path ?? (channel.encoding === 'standard' ? 'path appears with the first value Signal K maps' : '');
    return { via, value, age: channel.encoding === 'off' ? '' : age(channel.ageMs), stale, path };
  }

  function parameter(card, name, channel = null) {
    return card.parameters?.find(entry => entry.name === name && entry.channel === channel);
  }

  // An input bound to one parameter. change() returns the request body if the
  // value was edited, otherwise null; groups save all their changes at once.
  function editor(entry, options = {}) {
    if (!entry || entry.status !== 'done') return null;
    let element;
    let original;
    let read;
    let unit = '';
    if (options.choices) {
      // Numeric parameter with named values, e.g. the source of a standard PGN.
      element = document.createElement('select');
      for (const [value, label] of options.choices) element.append(Object.assign(text('option', label), { value: String(value) }));
      original = String(entry.value);
      if (!options.choices.some(([value]) => String(value) === original)) {
        element.append(Object.assign(text('option', `${original} (unknown)`), { value: original }));
      }
      read = () => element.value === original ? null : { value: Number(element.value) };
    } else if (entry.values) {
      element = document.createElement('select');
      for (const name of entry.values) element.append(Object.assign(text('option', options.labels?.[name] ?? name), { value: name }));
      original = entry.values[entry.value] ?? '';
      read = () => element.value === original ? null : { value: element.value };
    } else if (entry.type === 'string') {
      original = entry.value ?? '';
      read = () => element.value === original ? null : { value: element.value };
    } else if (options.factor) {
      original = (entry.value / options.factor).toFixed(6);
      read = () => element.value === original ? null : { value: Math.round(Number(element.value) * options.factor) };
    } else if (entry.si !== null && entry.si !== undefined) {
      const shown = display(entry.si, entry.siUnit, entry.absolute);
      original = shown.value;
      unit = shown.unit;
      read = () => element.value === original ? null : { si: fromDisplay(element.value, entry.siUnit, entry.absolute) };
    } else {
      original = String(entry.value);
      read = () => element.value === original ? null : { value: Number(element.value) };
    }
    if (!element) element = document.createElement('input');
    element.value = original;
    if (options.placeholder) element.placeholder = options.placeholder;
    element.disabled = entry.access !== 'rw';
    element.title = entry.description ?? '';
    element.setAttribute('aria-label', options.label ?? entry.name);
    return { element, unit: options.unit ?? unit, change: () => (entry.access === 'rw' ? read() : null), entry };
  }

  function fieldRow(label, edit, hint = '') {
    const row = text('div', '', 'field');
    row.append(text('span', label, 'field-label'));
    const value = text('span', '', 'field-value');
    if (edit) value.append(edit.element, text('span', edit.unit ?? '', 'field-unit'));
    else value.append(text('span', '–', 'muted'));
    row.append(value);
    if (hint) row.append(text('small', hint, 'field-hint'));
    return row;
  }

  // Stores the edited fields of one block; the button says what it saves.
  function saveButton(device, label, edits) {
    const writable = edits.filter(edit => edit && edit.entry.access === 'rw');
    if (!writable.length) return null;
    const element = button(label, async () => {
      const changes = writable.map(edit => [edit, edit.change()]).filter(([, body]) => body);
      if (!changes.length) { notice('Nothing changed'); return; }
      for (const [edit, body] of changes) {
        const result = edit.save ? await edit.save(body)
          : await api(`b1s/devices/${device.id}/parameters/${edit.entry.name}`, 'PUT', { channel: edit.entry.channel, ...body });
        if (result.status !== 'done') throw new Error(`${edit.entry.name}: ${result.status}`);
      }
      notice(`${changes.length} setting(s) stored on the device`);
      await loadParameters(device.id);
      render();
    }, 'Store the edited fields above on the device');
    element.classList?.add('primary');
    return element;
  }

  // A block of fields with its own actions underneath.
  function block(title, explanation, rows, actions) {
    const section = text('section', '', 'block');
    if (title) section.append(text('h4', title));
    if (explanation) section.append(text('p', explanation, 'block-text'));
    section.append(...rows);
    const bar = text('div', '', 'block-actions');
    bar.append(...actions.filter(Boolean));
    if (bar.children.length) section.append(bar);
    return section;
  }

  function devicePanel(device, card) {
    const role = editor(parameter(card, 'deviceRole'), { placeholder: 'e.g. fridgeSensor (unassigned)' });
    const address = editor(parameter(card, 'preferredAddress'));
    // The Signal K path prefix is a Pi-side setting of the device (not stored on it).
    const pluginPrefix = device.detail.pluginPrefix ?? '';
    const prefixInput = Object.assign(document.createElement('input'), { value: device.detail.pathPrefix ?? '', placeholder: 'none' });
    prefixInput.setAttribute('aria-label', 'Signal K path prefix of the device');
    const prefix = { element: prefixInput, unit: '', entry: { access: 'rw', name: 'pathPrefix' },
      change: () => prefixInput.value.trim() === (device.detail.pathPrefix ?? '') ? null : { pathPrefix: prefixInput.value.trim() },
      save: body => api(`b1s/devices/${device.id}/settings`, 'PUT', body).then(() => ({ status: 'done' })) };
    const example = [pluginPrefix, prefixInput.value.trim() || device.detail.pathPrefix, 'environment.inside.temperature'].filter(Boolean).join('.');
    const uptime = parameter(card, 'uptime');
    const seconds = uptime?.value ?? 0;
    const uptimeRow = text('div', '', 'field');
    uptimeRow.append(text('span', 'Uptime', 'field-label'), text('span',
      uptime ? `${Math.floor(seconds / 3600)} h ${Math.floor(seconds / 60) % 60} min (when read)` : '–', 'field-value'));
    const panel = text('section', '', 'device-settings');
    panel.append(text('h3', 'Device'), block('', '', [
      fieldRow('Role', role, 'What this device is for on the boat'),
      fieldRow('Preferred address', address, 'Address claimed first; takes effect after a restart'),
      fieldRow('Signal K path prefix', prefix, `Namespace for this device's B1S paths, below the plugin's${pluginPrefix ? ` (${pluginPrefix})` : ' (none set)'}: ` +
        `e.g. ${example}. Not for standard PGN channels: Signal K maps those itself. Stored on the Pi, part of the backup.`),
      uptimeRow],
    [saveButton(device, 'Save device settings', [role, address, prefix])]));
    return panel;
  }

  function setupPane(device, channel, card) {
    const label = editor(parameter(card, 'channelLabel', channel.channel), { placeholder: 'name for people' });
    const location = editor(parameter(card, 'channelLocation', channel.channel), { placeholder: 'unassigned' });
    const rows = [fieldRow('Label', label, 'Name shown for this channel'),
      fieldRow('Location', location, 'Signal K path fragment for the automatic path, e.g. inside.refrigerator; empty = unassigned')];
    const edits = [label, location];
    // Custom Signal K path: Pi-side, used instead of anything automatic.
    const pathInput = Object.assign(document.createElement('input'), { value: channel.customPath ?? '', placeholder: 'automatic' });
    pathInput.setAttribute('aria-label', 'Custom Signal K path');
    const customPath = { element: pathInput, unit: '', entry: { access: 'rw', name: 'path' },
      change: () => pathInput.value.trim() === (channel.customPath ?? '') ? null : { path: pathInput.value.trim() },
      save: body => api(`b1s/devices/${device.id}/channels/${channel.channel}/settings`, 'PUT', body).then(() => ({ status: 'done' })) };
    rows.push(fieldRow('Signal K path', customPath,
      'Empty: automatic (template + location, below the prefixes). A path is used exactly as written; ' +
      'starting with * it goes below the prefixes (*environment.cabin.temperature). Stored on the Pi. Not for standard PGN.'));
    edits.push(customPath);
    if (!channel.noStandard) {
      const encoding = editor(parameter(card, 'channelEncoding', channel.channel), { labels: encodingLabels });
      const sources = Object.entries(channel.standardSources ?? {}).map(([value, name]) => [Number(value), `${value} ${name}`]);
      const source = editor(parameter(card, 'channelStandardSource', channel.channel), sources.length ? { choices: sources } : {});
      const instance = editor(parameter(card, 'channelStandardInstance', channel.channel));
      rows.push(fieldRow('Sent as', encoding, 'automatic = B1S message. Standard PGN only if this value should look exactly ' +
          `like NMEA 2000 data: Signal K then files it by source and instance (PGN ${channel.standardPgn ?? ''}), next to the boat's data`),
        fieldRow('Standard PGN source', source, 'Only for standard PGN. Decides the Signal K path: sea or outside would be filed as the boat\'s own values'),
        fieldRow('Standard PGN instance', instance, 'Only for standard PGN; 255 = firmware default'));
      edits.push(encoding, source, instance);
    } else {
      rows.push(fieldRow('Sent as', null, 'B1S message (no standard PGN for this quantity)'));
    }
    return [block('', '', rows, [saveButton(device, 'Save setup', edits)]), outputBlock(device, channel, card)].filter(Boolean);
  }

  // --- small form controls ------------------------------------------------------

  function choice(options, value, label) {
    const element = document.createElement('select');
    for (const [name, text_] of Object.entries(options)) element.append(Object.assign(text('option', text_), { value: name }));
    element.value = value;
    element.setAttribute('aria-label', label);
    return element;
  }

  function numberField(value, label, placeholder = '') {
    const element = Object.assign(document.createElement('input'), { value: String(value ?? ''), placeholder, inputMode: 'decimal' });
    element.className = 'short';
    element.setAttribute('aria-label', label);
    return element;
  }

  function wholeNumber(input, name, maximum = 65535) {
    const number = input.value.trim() === '' ? 0 : Number(input.value);
    if (!Number.isInteger(number) || number < 0 || number > maximum) throw new Error(`${name}: a whole number 0..${maximum}`);
    return number;
  }

  // Seconds with one decimal -> tenths of a second (0 = empty).
  function tenths(input, name) {
    const number = input.value.trim() === '' ? 0 : Number(input.value);
    if (!Number.isFinite(number) || number < 0 || number > 6553.5) throw new Error(`${name}: 0.1..6553.5 s`);
    if (number !== 0 && number < 0.1) throw new Error(`${name}: at least 0.1 s`);
    return Math.round(number * 10);
  }

  // Unit people read for a value of a channel; differences of temperatures in °C too.
  function unitLabel(channel, perMinute = false) {
    const unit = channel.unit === 'K' ? '°C' : channel.unit === 'ratio' ? '%' : channel.unit ?? '';
    return perMinute ? `${unit}/min` : unit;
  }

  // Output: the function of the value that is sent, and how often it and its raw value go out.
  function outputBlock(device, channel, card) {
    const entry = parameter(card, 'channelOutput', channel.channel);
    if (!entry || entry.status !== 'done') return null;
    const current = entry.value;
    const fn = choice({ last: 'latest sample', average: 'average', minimum: 'minimum', maximum: 'maximum' },
      current.function === 'none' ? 'last' : current.function, 'Value sent');
    const length = numberField(current.window || '', 'Window length', 'e.g. 10');
    const samples = current.flags.includes('windowSamples');
    const lengthUnit = choice({ s: 's', samples: 'samples' }, samples ? 'samples' : 's', 'Window unit');
    const windowPart = text('span', '', 'inline-group');
    windowPart.append(text('span', 'over', 'muted'), length, lengthUnit);
    const period = numberField(current.period ? current.period / 10 : '', 'Send every', 'default');
    const rawPeriod = numberField(current.rawPeriod ? current.rawPeriod / 10 : '', 'Send the raw value every', 'never');
    const show = () => { windowPart.hidden = !windowed(fn.value); };
    fn.onchange = show;
    show();
    const read = () => ({
      function: fn.value,
      flags: lengthUnit.value === 'samples' ? ['windowSamples'] : [],
      window: windowed(fn.value) ? wholeNumber(length, 'Window') : 0,
      period: tenths(period, 'Send every'),
      rawPeriod: tenths(rawPeriod, 'Raw value')
    });
    const original = JSON.stringify(read());
    const edit = { element: text('span'), unit: '', entry,
      change: () => { const fields = read(); return JSON.stringify(fields) === original ? null : { value: fields }; } };
    const valueRow = text('div', '', 'field');
    const valueControls = text('span', '', 'field-value wrap');
    valueControls.append(fn, windowPart);
    valueRow.append(text('span', 'Value sent', 'field-label'), valueControls,
      text('small', 'Latest sample, or the average, minimum, or maximum over the last seconds or samples.', 'field-hint'));
    const periodRow = text('div', '', 'field');
    const periodControls = text('span', '', 'field-value');
    periodControls.append(period, text('span', `s${channel.period ? ` (now ${channel.period} s)` : ''}`, 'field-unit'));
    periodRow.append(text('span', 'Send every', 'field-label'), periodControls,
      text('small', 'Empty = firmware default. Shorter than the window: a rolling value (e.g. the 10 s average every second); ' +
        'equal to the window: one value per window. Only new samples are sent.', 'field-hint'));
    const rawRow = text('div', '', 'field');
    const rawControls = text('span', '', 'field-value');
    rawControls.append(rawPeriod, text('span', 's', 'field-unit'));
    rawRow.append(text('span', 'Raw value every', 'field-label'), rawControls,
      text('small', `Empty = not sent. The raw value as the device defines it, usually the latest sample, on ${channel.rawPath ?? 'the channel path + "Raw"'}.`, 'field-hint'));
    return block('Output', 'What the channel sends, and how often.', [valueRow, periodRow, rawRow],
      [saveButton(device, 'Save output', [edit])]);
  }

  function calibrationPane(device, channel, card) {
    const offset = editor(parameter(card, 'calibrationOffset', channel.channel));
    const gain = editor(parameter(card, 'calibrationGain', channel.channel), { factor: 1e6, unit: '×' });
    if (!offset && !gain) return [text('p', 'This channel has no calibration.', 'empty')];
    const unit = channel.unit === 'K' ? '°C' : channel.unit === 'ratio' ? '%' : channel.unit;
    const reference = Object.assign(document.createElement('input'), { placeholder: `e.g. ${unit === '°C' ? '21.5' : '50'}` });
    reference.setAttribute('aria-label', 'Reference value');
    const referenceRow = text('div', '', 'field');
    const referenceValue = text('span', '', 'field-value');
    referenceValue.append(reference, text('span', unit, 'field-unit'));
    referenceRow.append(text('span', 'Reference now', 'field-label'), referenceValue);
    const apply = (command, label, title) => button(label, async () => {
      if (!reference.value.trim()) throw new Error('Enter the reference value first');
      const result = await api(`b1s/devices/${device.id}/commands/${command}`, 'POST',
        { channel: channel.channel, si: fromDisplay(reference.value, channel.unit, true) });
      notice(`${label}: ${result.result}`, result.result !== 'done');
      await loadParameters(device.id);
      render();
    }, title);
    const reset = button('Reset calibration', async () => {
      if (!confirm(`Reset the calibration of ${channel.label || `channel ${channel.channel}`} (offset 0, gain 1)?`)) return;
      const result = await api(`b1s/devices/${device.id}/commands/resetCalibration`, 'POST', { channel: channel.channel });
      notice(`Reset: ${result.result}`, result.result !== 'done');
      await loadParameters(device.id);
      render();
    }, 'Offset 0, gain 1');
    reset.classList?.add('danger');
    return [
      block('Calibrate against a reference',
        'Measure the real value with a reference instrument and enter it. One point corrects the offset; ' +
        'a second point, at least one unit away, also corrects the gain.',
        [referenceRow],
        [apply('calibrate', 'Calibrate (offset)', 'The current reading becomes the reference value'),
          apply('calibrateSecondPoint', 'Add second point (gain)', 'Gain and offset from the first and this point')]),
      block('Calibration values', 'value = raw × gain + offset', [fieldRow('Offset', offset), fieldRow('Gain', gain)],
        [saveButton(device, 'Save calibration values', [offset, gain])]),
      block('', '', [], [reset])
    ];
  }

  // One alarm rule as a sentence: "[Warning] when the [average] over [10] [s]
  // is [above] [40] °C for [3] s; clears [2] °C back; [ ] latching".
  function alarmRow(channel, entry, state, onRemove) {
    const fields = entry.value;
    const draft = fields.function === 'none';
    const si = entry.si ?? {};
    const absolute = entry.absolute ?? {};
    const shown = name => fields[name] === undefined || draft ? '' : display(si[name], entry.siUnit, absolute[name]).value;
    const severity = choice({ warning: 'Warning', alarm: 'Alarm', emergency: 'Emergency' }, fields.severity ?? 'warning', 'Severity');
    const fn = choice({ last: 'latest value', average: 'average', minimum: 'minimum', maximum: 'maximum', change: 'change',
      rate: 'rate of change' }, draft ? 'last' : fields.function, 'Value');
    const length = numberField(fields.window || '', 'Window length', '10');
    const lengthUnit = choice({ s: 's', samples: 'samples' }, fields.flags?.includes('windowSamples') ? 'samples' : 's', 'Window unit');
    const comparison = choice(comparisonLabels, fields.comparison ?? 'above', 'Comparison');
    const threshold = numberField(shown('threshold'), 'Threshold');
    const upper = numberField(shown('upper'), 'Upper limit');
    const delay = numberField(draft ? '0' : String(fields.delay ?? 0), 'Delay in seconds');
    const hysteresis = numberField(draft ? '' : shown('hysteresis'), 'Hysteresis', '0');
    const latching = Object.assign(document.createElement('input'), { type: 'checkbox', checked: fields.flags?.includes('latching') ?? false });
    latching.setAttribute('aria-label', 'Latching');
    const unit = text('span', '', 'field-unit');
    const upperUnit = text('span', '', 'field-unit');
    const hysteresisUnit = text('span', '', 'field-unit');
    const windowPart = text('span', '', 'inline-group');
    windowPart.append(text('span', 'over', 'muted'), length, lengthUnit);
    const upperPart = text('span', '', 'inline-group');
    upperPart.append(text('span', 'and', 'muted'), upper, upperUnit);
    const update = () => {
      windowPart.hidden = !windowed(fn.value);
      upperPart.hidden = !['inside', 'outside'].includes(comparison.value);
      const perMinute = fn.value === 'rate';
      unit.textContent = upperUnit.textContent = hysteresisUnit.textContent = unitLabel(channel, perMinute);
    };
    fn.onchange = update;
    comparison.onchange = update;
    update();
    const sentence = text('div', '', 'alarm-sentence');
    sentence.append(severity, text('span', 'when the', 'muted'), fn, windowPart, text('span', 'is', 'muted'), comparison,
      threshold, unit, upperPart, text('span', 'for', 'muted'), delay, text('span', 's; clears', 'muted'), hysteresis,
      hysteresisUnit, text('span', 'back;', 'muted'));
    const latchLabel = text('label', '', 'inline-group');
    latchLabel.append(latching, ' latching');
    sentence.append(latchLabel);
    const footer = text('div', '', 'alarm-footer');
    footer.append(text('span', draft ? 'new, not saved yet' : state, `alarm-state${state === 'ACTIVE' ? ' active' : ''}`));
    if (onRemove) footer.append(button(draft ? 'Discard' : 'Remove', onRemove, draft ? 'Discard this new rule' : 'Remove this rule from the device'));
    const element = text('div', '', `alarm-row ${severity.value}`);
    severity.onchange = () => { element.className = `alarm-row ${severity.value}`; };
    element.append(sentence, footer);
    const read = () => {
      const band = ['inside', 'outside'].includes(comparison.value);
      const isAbsolute = !['change', 'rate'].includes(fn.value);
      if (threshold.value.trim() === '') throw new Error('Alarm: enter a threshold');
      if (band && upper.value.trim() === '') throw new Error('Alarm: enter the upper limit of the band');
      const flags = [];
      if (latching.checked) flags.push('latching');
      if (lengthUnit.value === 'samples') flags.push('windowSamples');
      return {
        value: { function: fn.value, comparison: comparison.value, severity: severity.value, flags,
          window: windowed(fn.value) ? wholeNumber(length, 'Window') : 0, delay: wholeNumber(delay, 'Delay'),
          threshold: 0, upper: 0, hysteresis: 0 },
        si: { threshold: fromDisplay(threshold.value, channel.unit, isAbsolute),
          upper: fromDisplay(band ? upper.value : threshold.value, channel.unit, isAbsolute),
          hysteresis: hysteresis.value.trim() === '' ? 0 : fromDisplay(hysteresis.value, channel.unit, false) }
      };
    };
    const snapshot = () => [severity, fn, length, lengthUnit, comparison, threshold, upper, delay, hysteresis]
      .map(control => control.value).concat(latching.checked).join('|');
    const original = draft ? null : snapshot();
    return { element, edit: { element, unit: '', entry, change: () => (snapshot() === original ? null : read()) } };
  }

  function alarmsPane(device, channel, card) {
    const fault = parameter(card, 'faultAlarm', channel.channel);
    if (!fault || fault.status !== 'done') return [text('p', 'This channel has no alarms.', 'empty')];
    const stateBits = parameter(card, 'alarmState', channel.channel)?.value ?? 0;
    const stateOf = slot => (stateBits >> (2 * slot) & 1 ? (stateBits >> (2 * slot + 1) & 1 ? 'active, acknowledged' : 'ACTIVE') : 'ok');
    const edits = [];
    // Fault alarm: always there, from off to emergency.
    const faultSeverity = choice(severityLabels, fault.value.severity, 'Sensor fault alarm');
    const faultDelay = numberField(String(fault.value.delay), 'Sensor fault delay in seconds');
    const faultLatching = Object.assign(document.createElement('input'), { type: 'checkbox', checked: fault.value.flags.includes('latching') });
    faultLatching.setAttribute('aria-label', 'Sensor fault alarm latching');
    const faultSnapshot = () => [faultSeverity.value, faultDelay.value, faultLatching.checked].join('|');
    const faultOriginal = faultSnapshot();
    edits.push({ element: faultSeverity, unit: '', entry: fault, change: () => (faultSnapshot() === faultOriginal ? null
      : { value: { severity: faultSeverity.value, flags: faultLatching.checked ? ['latching'] : [], delay: wholeNumber(faultDelay, 'Delay') } }) });
    const faultSentence = text('div', '', 'alarm-sentence');
    const faultLatch = text('label', '', 'inline-group');
    faultLatch.append(faultLatching, ' latching');
    faultSentence.append(faultSeverity, text('span', 'when the sensor gives no valid value for', 'muted'), faultDelay,
      text('span', 's;', 'muted'), faultLatch);
    const faultFooter = text('div', '', 'alarm-footer');
    const faultState = stateOf(4);
    faultFooter.append(text('span', fault.value.severity === 'off' ? 'off' : faultState, `alarm-state${faultState === 'ACTIVE' ? ' active' : ''}`));
    const faultRow = text('div', '', `alarm-row ${fault.value.severity}`);
    faultRow.append(faultSentence, faultFooter);
    // Rules: the used slots and drafts added on the page.
    card.alarmDrafts ??= new Map(); // channel -> Set of slots being added
    const drafts = card.alarmDrafts.get(channel.channel) ?? new Set();
    card.alarmDrafts.set(channel.channel, drafts);
    const rows = [];
    const draftRows = []; // new rules come last
    const free = [];
    for (let slot = 0; slot < 4; slot += 1) {
      const entry = parameter(card, `alarm${slot}`, channel.channel);
      if (!entry || entry.status !== 'done') continue;
      const used = entry.value.function !== 'none';
      if (!used && !drafts.has(slot)) { free.push(slot); continue; }
      const remove = async () => {
        if (!used) { drafts.delete(slot); card.version += 1; render(); return; }
        if (!confirm('Remove this alarm rule from the device?')) return;
        const result = await api(`b1s/devices/${device.id}/parameters/alarm${slot}`, 'PUT',
          { channel: channel.channel, value: { ...entry.value, function: 'none' } });
        if (result.status !== 'done') throw new Error(`Remove: ${result.status}`);
        notice('Alarm rule removed');
        await loadParameters(device.id);
        render();
      };
      const row = alarmRow(channel, entry, stateOf(slot), remove);
      (used ? rows : draftRows).push(row.element);
      edits.push(row.edit);
    }
    const add = free.length ? button('+ Add alarm', async () => { drafts.add(free[0]); card.version += 1; render(); },
      'A new rule; it is stored with Save alarms') : null;
    const pool = parameter(card, 'windowPool')?.value;
    const poolText = pool ? `Average, minimum, maximum, and change use a window each: ${pool.free} of ${pool.windows} free on this device, ` +
      `${pool.buckets} buckets per window (a window of 60 s is accurate to ${Math.round(60 / pool.buckets * 10) / 10} s).` : '';
    const reset = button('Reset alarms and output to firmware defaults', async () => {
      if (!confirm(`Put the alarms and output of ${channel.label || `channel ${channel.channel}`} back to the firmware defaults?`)) return;
      const result = await api(`b1s/devices/${device.id}/commands/resetAlarmsAndOutput`, 'POST', { channel: channel.channel });
      notice(`Reset: ${result.result}`, result.result !== 'done');
      drafts.clear();
      await loadParameters(device.id);
      render();
    }, 'Alarm rules, sensor fault alarm, output function, and send periods as compiled into the firmware');
    reset.classList?.add('danger');
    // Saved drafts come back as used rules.
    const save = saveButton(device, 'Save alarms', edits);
    if (save) {
      const store = save.onclick;
      save.onclick = async () => { drafts.clear(); await store(); };
    }
    return [
      block('Sensor fault', '', [faultRow], []),
      block('Alarm rules', rows.length + draftRows.length ? `Up to 4 rules. ${poolText}` : `No rules yet. ${poolText}`,
        [...rows, ...draftRows], [add]),
      block('', '', [], [save, reset])
    ];
  }

  const channelTabs = [['setup', 'Setup', setupPane], ['calibration', 'Calibration', calibrationPane], ['alarms', 'Alarms', alarmsPane]];

  function channelPanel(device, channel, card) {
    const panel = document.createElement('details');
    panel.className = 'channel';
    panel.open = card.openChannels.has(channel.channel);
    panel.ontoggle = () => { if (panel.open) card.openChannels.add(channel.channel); else card.openChannels.delete(channel.channel); };
    const summary = text('summary', '');
    const state = channelState(channel);
    const live = text('span', state.value, `channel-value${channel.encoding === 'off' ? ' off' : ''}`);
    live.dataset.channel = String(channel.channel);
    const when = text('span', state.age, `channel-age${state.stale ? ' stale' : ''}`);
    when.dataset.age = String(channel.channel);
    summary.append(text('span', `${channel.channel}`, 'channel-number'), text('span', channel.label || `Channel ${channel.channel}`, 'channel-name'),
      text('span', channel.quantity ?? '', 'muted'), text('span', state.via, `channel-via${channel.encoding === 'off' ? ' off' : ''}`),
      live, when, text('span', state.path, `channel-path${channel.encoding === 'off' ? ' off' : ''}`));
    if (channel.conflicts?.length) {
      summary.append(text('span', `⚠ This Signal K path also receives values from ${channel.conflicts.join(', ')}: ` +
        'the channel mixes with or overwrites them. Choose another path or location.', 'channel-conflict'));
    }
    panel.append(summary);
    if (!card.parameters) {
      panel.append(text('p', 'Reading the device’s settings …', 'empty'));
      return panel;
    }
    const current = card.channelTab.get(channel.channel) ?? 'setup';
    const tabs = text('div', '', 'channel-tabs');
    tabs.setAttribute('role', 'tablist');
    for (const [id, label] of channelTabs) {
      const tab = text('button', label, `channel-tab${id === current ? ' selected' : ''}`);
      tab.setAttribute('role', 'tab');
      tab.setAttribute('aria-selected', String(id === current));
      tab.onclick = () => { card.channelTab.set(channel.channel, id); card.version += 1; render(); };
      tabs.append(tab);
    }
    const pane = text('div', '', 'channel-pane');
    pane.append(...channelTabs.find(([id]) => id === current)[2](device, channel, card));
    panel.append(tabs, pane);
    return panel;
  }

  // Log panel: the device streams its log only when asked, for a fixed time,
  // rate limited, and stops by itself (src/lib/b1s/CONFIGURATION.md).
  const levels = { D: 'debug', I: 'info', W: 'warning', E: 'error' };

  function filteredLog(card) {
    return card.logText.split('\n').filter(line => {
      const level = /^\[\s*[\d.]+\] ([DIWE]) /.exec(line)?.[1];
      return !level || card.levels.has(level);
    }).join('\n');
  }

  function showLog(card) {
    if (!card.logOutput) return;
    const atBottom = card.logOutput.scrollTop + card.logOutput.clientHeight >= card.logOutput.scrollHeight - 4;
    card.logOutput.textContent = filteredLog(card);
    if (atBottom) card.logOutput.scrollTop = card.logOutput.scrollHeight;
    const remaining = card.logRemaining ?? 0;
    card.logState.textContent = remaining > 0
      ? `streaming, ${Math.floor(remaining / 60)}:${String(remaining % 60).padStart(2, '0')} left (at most 512 B/s; the device stops by itself)`
      : 'off';
    card.logState.className = remaining > 0 ? 'log-state streaming' : 'log-state';
  }

  function logPanel(device, card) {
    const panel = document.createElement('details');
    panel.className = 'log';
    panel.open = card.logOpen;
    panel.append(text('summary', 'Log'));
    const controls = text('div', '', 'log-controls');
    card.logState = text('span', '', 'log-state');
    const stream = minutes => button(minutes ? `${minutes} min` : 'Stop', async () => {
      const result = await api(`b1s/devices/${device.id}/log`, 'POST', { minutes });
      card.logRemaining = result.remaining;
      showLog(card);
    }, minutes ? `Stream the device log for ${minutes} minutes` : 'Stop the log stream now');
    controls.append(text('span', 'Stream:', 'log-label'), stream(5), stream(15), stream(60), stream(0), card.logState);
    const filter = text('div', '', 'log-filter');
    for (const [letter, name] of Object.entries(levels)) {
      const label = text('label', '');
      const box = Object.assign(document.createElement('input'), { type: 'checkbox', checked: card.levels.has(letter) });
      box.onchange = () => { if (box.checked) card.levels.add(letter); else card.levels.delete(letter); showLog(card); };
      label.append(box, ` ${name}`);
      filter.append(label);
    }
    filter.append(button('Clear', async () => { card.logText = ''; showLog(card); }, 'Clear the shown log'));
    card.logOutput = text('pre', '');
    const input = Object.assign(document.createElement('input'), { placeholder: 'Console: help, sensors, health, supply … (Enter; streams its output for 30 s)' });
    input.setAttribute('aria-label', 'Console command');
    input.onkeydown = event => {
      if (event.key !== 'Enter' || !input.value.trim()) return;
      void api(`b1s/devices/${device.id}/console`, 'POST', { line: input.value }).catch(error => notice(error.message, true));
      input.value = '';
    };
    panel.ontoggle = () => { card.logOpen = panel.open; };
    panel.append(controls, filter, card.logOutput, input);
    showLog(card);
    return panel;
  }

  function renderB1s(device) {
    const info = device.detail;
    const card = b1sCard(device.id);
    if (!card.parameters && !card.loading) void loadParameters(device.id).then(render).catch(error => notice(error.message, true));
    const header = text('div', '', 'device-header');
    const title = text('div');
    title.append(text('h2', device.label), text('span', device.status, `badge ${device.online ? '' : 'offline'}`));
    const controls = text('div', '', 'install-controls');
    controls.append(
      button('Identify', async () => notice(`Identify: ${(await api(`b1s/devices/${device.id}/commands/identify`, 'POST', { argument: 5 })).result}`),
        'Blink the device for 5 s'),
      button('Read settings', async () => { await loadParameters(device.id); render(); notice('Settings read from the device and backed up'); },
        'Read all settings from the device again (also stores the backup)'),
      button('Restore…', async () => {
        const backups = await api('b1s/backups');
        const own = backups.find(backup => backup.deviceId === Number(device.id));
        const choices = backups.map(backup => `${backup.deviceId} (${backup.product?.model ?? '?'}, ${backup.savedAt})`).join('\n');
        const from = prompt(`Restore the configuration of which device ID?\n${choices}`, own ? device.id : '');
        if (!from) return;
        const result = await api(`b1s/devices/${device.id}/restore`, 'POST', { from: Number(from) });
        notice(`Restored ${result.restored} of ${result.results.length} parameter(s)`);
        await loadParameters(device.id);
      }, 'Write a backup onto this device: its own, or a replaced device\'s'),
      button('Restart', async () => {
        if (!confirm(`Restart ${device.label}?`)) return;
        notice(`Restart: ${(await api(`b1s/devices/${device.id}/commands/restart`, 'POST', {})).result}`);
      }));
    header.append(title, controls);
    const product = info.product ?? {};
    const meta = text('p', [`B1S · ${device.address}`, `device ID ${device.id}`, product.software, product.hardware,
      product.code !== undefined && product.code !== null ? `product ${product.code}` : null].filter(Boolean).join(' · '), 'device-meta');
    const channels = text('div', '', 'channels');
    channels.append(...(info.channels ?? []).map(channel => channelPanel(device, channel, card)));
    detail.replaceChildren(header, meta, card.parameters ? devicePanel(device, card) : text('p', 'Reading the device’s settings …', 'empty'),
      text('h3', 'Channels'), channels, logPanel(device, card));
  }

  function updateB1s(device) {
    for (const channel of device.detail?.channels ?? []) {
      const state = channelState(channel);
      const live = detail.querySelector(`.channel-value[data-channel="${channel.channel}"]`);
      if (live) live.textContent = state.value;
      const when = detail.querySelector(`.channel-age[data-age="${channel.channel}"]`);
      if (when) {
        when.textContent = state.age;
        when.className = `channel-age${state.stale ? ' stale' : ''}`;
      }
    }
  }

  // The log keeps coming in while the panel is closed; it is polled for the
  // selected device.
  async function pollLog(device) {
    const card = b1sCard(device.id);
    const output = await api(`b1s/devices/${device.id}/console?since=${card.logSince}`);
    card.logSince = output.next;
    card.logRemaining = output.remaining;
    if (output.text) card.logText = (card.logText + output.text).slice(-60000);
    showLog(card);
  }

  // --- detail -----------------------------------------------------------------

  function render() {
    renderList();
    const device = devices.find(entry => keyOf(entry) === selected);
    if (!device) {
      rendered.key = null;
      detail.replaceChildren(text('p', devices.length ? 'Select a device' : '', 'empty'));
      return;
    }
    // Rebuild when the structure changed; otherwise only live values, so
    // inputs keep what is being typed.
    const structure = device.kind === 'b1s'
      ? [device.detail.address, device.detail.product, device.detail.pathPrefix, device.detail.channels.map(channel =>
        [channel.channel, channel.label, channel.location, channel.encoding, channel.path, channel.customPath, channel.conflicts]),
        b1sCard(device.id).version]
      : [device.status, (device.detail?.subscriptions ?? []).map(source => source.sourceIndex), catalogNames,
        device.detail?.lastTransfer, device.detail?.error];
    const key = JSON.stringify([keyOf(device), structure]);
    if (key !== rendered.key) {
      rendered.key = key;
      if (device.kind === 'b1s') renderB1s(device);
      else renderDisplay(device);
    } else if (device.kind === 'b1s') updateB1s(device);
    else updateDisplay(device);
  }

  async function poll() {
    if (view.hidden) return;
    try {
      devices = (await api('devices')).devices;
      if (!selected && devices.length) selected = keyOf(devices[0]);
      render();
      const device = devices.find(entry => keyOf(entry) === selected);
      if (device?.kind === 'b1s') await pollLog(device);
    } catch (error) {
      document.getElementById('connection').textContent = 'Unavailable';
      notice(error.message, true);
    }
  }

  async function refreshNames() {
    try { catalogNames = (await api('configurations')).names; } catch { /* the designer reports it */ }
  }

  void refreshNames();
  setInterval(refreshNames, 5000);
  setInterval(poll, 1000);
  void poll();
})();
