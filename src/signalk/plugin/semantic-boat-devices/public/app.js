'use strict';

const base = new URL('/plugins/semantic-boat-devices/', window.location.origin);
const element = id => document.getElementById(id);
let names = [];
let templates = {};
let savedText = '';
let selectedName = null;
let signalPaths = [];
let editorSection = 0;
let editorPage = 0;
let editorWidget = 0;
const openSourcePanels = new Set();

async function api(route, method = 'GET', body) {
  const response = await fetch(new URL(route, base), { method,
    headers: body === undefined ? {} : { 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body) });
  if (!response.headers.get('content-type')?.includes('application/json')) {
    if (response.status === 401 || response.status === 403) {
      throw new Error('Sign in to Signal K, then reload this page.');
    }
    throw new Error(`API ${route}: expected JSON, received HTTP ${response.status}`);
  }
  const result = await response.json();
  if (!response.ok) throw new Error(result.error ?? `HTTP ${response.status}`);
  return result;
}

function notice(message, error = false) {
  element('notice').hidden = false;
  element('notice').textContent = message;
  element('notice').className = error ? 'error' : '';
}

function text(tag, content, className = '') {
  const node = document.createElement(tag); node.textContent = content; node.className = className; return node;
}

function icon(name, label, action) {
  const button = document.createElement('button'); button.className = 'icon';
  button.title = label; button.setAttribute('aria-label', label);
  const symbol = document.createElement('i'); symbol.dataset.lucide = name;
  button.append(symbol); button.onclick = action; return button;
}

function updateEditorStatus() {
  element('byte-count').textContent = `${new TextEncoder().encode(element('json').value).length} bytes`;
  element('editor-status').textContent = element('json').value === savedText && element('filename').value === selectedName ? 'Saved' : 'Unsaved';
}

function setDocument(name, document, saved) {
  element('filename').value = name; element('json').value = JSON.stringify(document, null, 2);
  selectedName = saved ? name : null; savedText = saved ? element('json').value : '';
  editorSection = 0; editorPage = 0; editorWidget = 0;
  updateEditorStatus(); renderCatalog(); renderProperties();
}

function collectPaths(tree, prefix = '', result = []) {
  if (!tree || typeof tree !== 'object') return result;
  if (Object.hasOwn(tree, 'value')) {
    if (prefix === 'navigation.position' && tree.value &&
        Number.isFinite(tree.value.latitude) && Number.isFinite(tree.value.longitude)) {
      result.push({ path: prefix, unit: '', value: `${tree.value.latitude}, ${tree.value.longitude}`,
        label: tree.meta?.displayName ?? tree.meta?.description ?? 'Position' });
      return result;
    }
    if (prefix && (typeof tree.value === 'string' || (typeof tree.value === 'number' && Number.isFinite(tree.value)))) {
      result.push({ path: prefix, unit: tree.meta?.units ?? '', value: tree.value,
        label: tree.meta?.displayName ?? tree.meta?.description ?? '' });
    }
    return result;
  }
  for (const [key, child] of Object.entries(tree)) {
    if (key.startsWith('$') || ['meta', 'values', 'timestamp'].includes(key)) continue;
    collectPaths(child, prefix ? `${prefix}.${key}` : key, result);
  }
  return result;
}

async function refreshPaths() {
  const paths = collectPaths(await api('/signalk/v1/api/vessels/self')).sort((left, right) => left.path.localeCompare(right.path));
  signalPaths = paths;
  const list = element('signal-paths'); list.replaceChildren();
  for (const entry of paths) {
    const option = text('option', `${entry.value} ${entry.unit} ${entry.label}`.trim());
    option.value = entry.path; list.append(option);
  }
  element('path-count').textContent = `${paths.length} paths`;
}

function editDocument(change, rerender = false) {
  try {
    const document = JSON.parse(element('json').value);
    change(document);
    element('json').value = JSON.stringify(document, null, 2);
    updateEditorStatus();
    element('notice').hidden = true;
    if (rerender) renderProperties();
    return true;
  } catch (error) { notice(error.message, true); return false; }
}

function propertyField(container, label, value, change, options = {}) {
  const wrapper = text('label', label, 'property-field');
  const input = document.createElement(options.choices ? 'select' : 'input');
  input.setAttribute('aria-label', label);
  if (options.choices) {
    const choices = [...options.choices];
    if (value && !choices.some(choice => (typeof choice === 'string' ? choice : choice.value) === value)) choices.push(value);
    for (const choice of choices) {
      const option = text('option', typeof choice === 'string' ? choice : choice.label);
      option.value = typeof choice === 'string' ? choice : choice.value; input.append(option);
    }
  } else input.type = options.checkbox ? 'checkbox' : options.number ? 'number' : 'text';
  input.value = value ?? '';
  if (options.checkbox) input.checked = Boolean(value);
  if (options.number) { input.min = options.min ?? 0; input.max = options.max ?? 4294967295; input.step = options.float ? 'any' : 1; }
  if (options.path) input.setAttribute('list', 'signal-paths');
  input.oninput = () => { element('editor-status').textContent = 'Unsaved'; };
  input.onchange = () => {
    if (!input.reportValidity()) { input.value = value ?? ''; updateEditorStatus(); return; }
    if (change(options.checkbox ? input.checked : options.number ? (input.value === '' ? undefined : Number(input.value)) : input.value) === false) {
      input.value = value ?? '';
      if (options.checkbox) input.checked = Boolean(value);
    }
  };
  wrapper.append(input); container.append(wrapper); return input;
}

function historyBytes(document) {
  return [...(document.permanent_sources ?? []), ...document.sections.flatMap(section => section.sources ?? [])]
    .reduce((sum, source) => sum + (source.history ? 32 + 4 * source.history.points : 0), 0);
}

function widgetFits(page, widget, ignored = -1) {
  const cell = widget.cell ?? {};
  const width = cell.column_span ?? 1; const height = cell.row_span ?? 1;
  if (cell.column < 0 || cell.row < 0 || cell.column + width > page.grid.columns || cell.row + height > page.grid.rows) return false;
  return !page.widgets.some((other, index) => index !== ignored &&
    cell.column < other.cell.column + (other.cell.column_span ?? 1) && cell.column + width > other.cell.column &&
    cell.row < other.cell.row + (other.cell.row_span ?? 1) && cell.row + height > other.cell.row);
}

function newId(entries, prefix) {
  let suffix = entries.length + 1;
  while (entries.some(entry => entry.id === `${prefix}_${suffix}`)) suffix += 1;
  return `${prefix}_${suffix}`;
}

function renderLayout(container, configuration) {
  if (!configuration.sections?.length) return;
  editorSection = Math.min(editorSection, configuration.sections.length - 1);
  const section = configuration.sections[editorSection];
  editorPage = Math.min(editorPage, Math.max(0, section.pages.length - 1));
  const selection = text('div', '', 'property-heading hierarchy-heading');
  selection.append(text('h2', 'Sections'));
  const sectionActions = text('div', '', 'hierarchy-actions');
  for (const [symbol, label, offset] of [['arrow-left', 'Move section earlier', -1], ['arrow-right', 'Move section later', 1]]) {
    const move = icon(symbol, label, () => {
      const target = editorSection + offset;
      if (target < 0 || target >= configuration.sections.length) return;
      editDocument(document => {
        const currentKey = `section-${editorSection}`; const targetKey = `section-${target}`;
        const currentOpen = openSourcePanels.has(currentKey); const targetOpen = openSourcePanels.has(targetKey);
        [document.sections[editorSection], document.sections[target]] = [document.sections[target], document.sections[editorSection]];
        openSourcePanels.delete(currentKey); openSourcePanels.delete(targetKey);
        if (currentOpen) openSourcePanels.add(targetKey);
        if (targetOpen) openSourcePanels.add(currentKey);
        editorSection = target;
      }, true);
    });
    move.disabled = editorSection + offset < 0 || editorSection + offset >= configuration.sections.length;
    sectionActions.append(move);
  }
  sectionActions.append(icon('plus', 'Add section', () => editDocument(document => {
    document.sections.push({ id: newId(document.sections, 'section'), title: 'NEW SECTION', sources: [],
      pages: [{ id: 'page_1', title: 'NEW PAGE', grid: { columns: 2, rows: 2, gap: 4 }, widgets: [] }] });
    editorSection = document.sections.length - 1; editorPage = 0; editorWidget = 0;
  }, true)));
  const deleteSection = icon('trash-2', 'Delete section', () => {
    if (confirm(`Delete section ${section.title || section.id} and all its pages and sources?`)) editDocument(document => { document.sections.splice(editorSection, 1); editorSection = 0; editorPage = 0; editorWidget = 0; }, true);
  });
  deleteSection.disabled = configuration.sections.length <= 1; sectionActions.append(deleteSection);
  selection.append(sectionActions);
  container.append(selection);
  const sections = text('div', '', 'section-tabs'); sections.setAttribute('role', 'tablist'); sections.setAttribute('aria-label', 'Sections');
  for (const [index, entry] of configuration.sections.entries()) {
    const button = text('button', entry.title || entry.id, `section-tab ${index === editorSection ? 'selected' : ''}`);
    button.id = `section-tab-${index}`; button.tabIndex = index === editorSection ? 0 : -1;
    button.setAttribute('role', 'tab'); button.setAttribute('aria-selected', String(index === editorSection));
    button.setAttribute('aria-controls', 'section-properties');
    button.onclick = () => { editorSection = index; editorPage = 0; editorWidget = 0; renderProperties(); };
    button.onkeydown = event => {
      if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) return;
      event.preventDefault();
      editorSection = event.key === 'Home' ? 0 : event.key === 'End' ? configuration.sections.length - 1
        : (index + (event.key === 'ArrowRight' ? 1 : -1) + configuration.sections.length) % configuration.sections.length;
      editorPage = 0; editorWidget = 0; renderProperties();
      element('properties').querySelectorAll('.section-tab')[editorSection].focus();
    };
    sections.append(button);
  }
  container.append(sections);
  const sectionPanel = text('div', '', 'section-panel'); sectionPanel.id = 'section-properties'; sectionPanel.setAttribute('role', 'tabpanel');
  sectionPanel.setAttribute('aria-labelledby', `section-tab-${editorSection}`); container.append(sectionPanel);
  const sectionFields = text('div', '', 'property-grid');
  propertyField(sectionFields, 'Section title', section.title, value => editDocument(document => { document.sections[editorSection].title = value; }, true));
  propertyField(sectionFields, 'Section ID', section.id, value => editDocument(document => {
    if (!value || document.sections.some((entry, index) => index !== editorSection && entry.id === value)) throw new Error('Section IDs must be non-empty and unique');
    document.sections[editorSection].id = value;
  }));
  sectionPanel.append(sectionFields);
  renderSources(sectionPanel, configuration, { title: 'Section sources', address: editorSection, sources: section.sources ?? [] });
  const workspace = text('div', '', 'page-workspace'); sectionPanel.append(workspace);
  const pageBrowser = text('div', '', 'page-browser'); workspace.append(pageBrowser);
  const pages = text('div', '', 'property-heading'); pages.append(text('h2', 'Pages'));
  const pageActions = text('div', '', 'hierarchy-actions');
  for (const [symbol, label, offset] of [['arrow-up', 'Move page earlier', -1], ['arrow-down', 'Move page later', 1]]) {
    const move = icon(symbol, label, () => {
      const target = editorPage + offset;
      if (target < 0 || target >= section.pages.length) return;
      editDocument(document => {
        const entries = document.sections[editorSection].pages;
        [entries[editorPage], entries[target]] = [entries[target], entries[editorPage]];
        editorPage = target;
      }, true);
    });
    move.disabled = editorPage + offset < 0 || editorPage + offset >= section.pages.length;
    pageActions.append(move);
  }
  pageActions.append(icon('plus', 'Add page', () => editDocument(document => {
    const entries = document.sections[editorSection].pages;
    entries.push({ id: newId(entries, 'page'), title: 'NEW PAGE', grid: { columns: 2, rows: 2, gap: 4 }, widgets: [] });
    editorPage = entries.length - 1; editorWidget = 0;
  }, true)));
  const deletePage = icon('trash-2', 'Delete page', () => {
    if (confirm(`Delete page ${section.pages[editorPage]?.title || section.pages[editorPage]?.id}?`)) editDocument(document => {
      document.sections[editorSection].pages.splice(editorPage, 1); editorPage = 0; editorWidget = 0;
    }, true);
  });
  deletePage.disabled = section.pages.length <= 1; pageActions.append(deletePage); pages.append(pageActions); pageBrowser.append(pages);
  const pageList = text('div', '', 'page-list'); pageList.setAttribute('aria-label', 'Pages');
  for (const [index, entry] of section.pages.entries()) {
    const button = text('button', '', `page-choice ${index === editorPage ? 'selected' : ''}`);
    button.append(text('span', entry.title || entry.id), text('small', `${entry.widgets.length} widgets`));
    button.setAttribute('aria-label', `Page: ${entry.title || entry.id}`);
    if (index === editorPage) button.setAttribute('aria-current', 'page');
    button.onclick = () => { editorPage = index; editorWidget = 0; renderProperties(); };
    pageList.append(button);
  }
  pageBrowser.append(pageList);
  const pagePanel = text('div', '', 'page-panel'); workspace.append(pagePanel);
  container = pagePanel;
  const page = section.pages[editorPage]; if (!page) return;
  const pageHeading = text('h2', page.title || page.id, 'page-title'); container.append(pageHeading);
  const currentPage = document => document.sections[editorSection].pages[editorPage];
  const fields = text('div', '', 'property-grid');
  propertyField(fields, 'Page title', page.title, value => editDocument(document => { currentPage(document).title = value; }, true));
  propertyField(fields, 'Page ID', page.id, value => editDocument(document => {
    if (!value || document.sections[editorSection].pages.some((entry, index) => index !== editorPage && entry.id === value)) throw new Error('Page IDs must be non-empty and unique');
    currentPage(document).id = value;
  }));
  for (const [key, label] of [['columns', 'Columns'], ['rows', 'Rows'], ['gap', 'Gap']]) {
    propertyField(fields, label, page.grid[key] ?? 0, value => editDocument(document => {
      if (value === undefined) throw new Error(`${label} is required`);
      const target = currentPage(document); target.grid[key] = value;
      if (target.widgets.some(widget => widget.cell.column + (widget.cell.column_span ?? 1) > target.grid.columns || widget.cell.row + (widget.cell.row_span ?? 1) > target.grid.rows)) throw new Error('Grid must contain all widgets');
    }, true), { number: true, min: key === 'gap' ? 0 : 1, max: 255 });
  }
  container.append(fields);
  const available = [...new Map([...(configuration.permanent_sources ?? []), ...(section.sources ?? [])]
    .map(source => [source.id, source])).values()];
  const histories = available.filter(source => source.history && source.provider !== 'signalk-text');
  const plotFields = text('div', '', 'property-grid');
  container.append(text('h3', 'History plot'));
  propertyField(plotFields, 'History source', page.history_plot?.source ?? '', value => editDocument(document => {
    const target = currentPage(document);
    if (!value) delete target.history_plot;
    else target.history_plot = { ...(target.history_plot ?? {}), source: value };
  }, true), { choices: [{ value: '', label: 'None' }, ...histories.map(source => ({ value: source.id, label: source.id }))] });
  if (page.history_plot) {
    const changePlot = (key, value) => editDocument(document => {
      const plot = currentPage(document).history_plot;
      if (value === undefined) delete plot[key]; else plot[key] = value;
      if (plot.y_min !== undefined && !(plot.y_min < plot.y_max)) throw new Error('Y minimum must be less than Y maximum');
    });
    propertyField(plotFields, 'Graph title', page.history_plot.title, value => changePlot('title', value));
    propertyField(plotFields, 'Graph unit', page.history_plot.display_unit, value => changePlot('display_unit', value));
    propertyField(plotFields, 'Graph decimals', page.history_plot.decimals ?? 1, value => changePlot('decimals', value), { number: true, max: 6 });
    propertyField(plotFields, 'Fixed Y range', page.history_plot.y_min !== undefined, value => editDocument(document => {
      const plot = currentPage(document).history_plot;
      if (value) { plot.y_min = 0; plot.y_max = 100; }
      else { delete plot.y_min; delete plot.y_max; }
    }, true), { checkbox: true });
    if (page.history_plot.y_min !== undefined) {
      for (const [key, label] of [['y_min', 'Y minimum'], ['y_max', 'Y maximum']])
        propertyField(plotFields, label, page.history_plot[key], value => {
          if (value === undefined) return false;
          return changePlot(key, value);
        }, { number: true, float: true, min: -3.4028234663852886e38, max: 3.4028234663852886e38 });
    }
  }
  container.append(plotFields);
  const heading = text('div', '', 'property-heading layout-heading'); heading.append(text('h2', 'Widgets'));
  heading.append(icon('plus', 'Add widget', () => editDocument(document => {
    const target = currentPage(document); let cell;
    for (let row = 0; row < target.grid.rows && !cell; row += 1) for (let column = 0; column < target.grid.columns && !cell; column += 1) {
      if (widgetFits(target, { cell: { column, row } })) cell = { column, row };
    }
    if (!cell) throw new Error('No empty cells; enlarge the grid or remove a widget');
    target.widgets.push(available.length ? { type: 'value', source: available[0].id, label: 'VALUE', cell }
      : { type: 'text', text: 'Text', cell });
    editorWidget = target.widgets.length - 1;
  }, true)));
  container.append(heading);
  const preview = text('div', '', 'layout-preview'); preview.setAttribute('aria-label', 'Page layout');
  preview.style.gridTemplateColumns = `repeat(${page.grid.columns},minmax(0,1fr))`;
  preview.style.gridTemplateRows = `repeat(${page.grid.rows},minmax(0,1fr))`;
  editorWidget = Math.min(editorWidget, Math.max(0, page.widgets.length - 1));
  for (const [index, widget] of page.widgets.entries()) {
    const tile = text('button', '', `layout-widget ${index === editorWidget ? 'selected' : ''}`);
    tile.title = widget.label || widget.text || widget.type;
    tile.setAttribute('aria-label', `Edit widget ${index + 1}: ${tile.title}`);
    tile.append(text('span', widget.label || widget.text || widget.type), text('small', widget.source || widget.type));
    if (widgetFits(page, widget, index)) {
      tile.style.gridColumn = `${widget.cell.column + 1} / span ${widget.cell.column_span ?? 1}`;
      tile.style.gridRow = `${widget.cell.row + 1} / span ${widget.cell.row_span ?? 1}`;
      preview.append(tile);
    } else { tile.append(text('small', 'Invalid placement')); container.append(tile); }
    tile.onclick = () => { editorWidget = index; renderProperties(); };
  }
  container.append(preview);
  const widget = page.widgets[editorWidget]; if (!widget) return;
  const widgetFields = text('div', '', 'property-grid widget-fields');
  container.append(text('h3', `Widget ${editorWidget + 1}: ${widget.label || widget.text || widget.type}`, 'widget-title'));
  const changeWidget = (key, value) => editDocument(document => { currentPage(document).widgets[editorWidget][key] = value; }, true);
  propertyField(widgetFields, 'Widget type', widget.type, value => editDocument(document => {
    const entry = currentPage(document).widgets[editorWidget]; entry.type = value;
    if (value === 'value' && !entry.source && available.length) entry.source = available[0].id;
    if (value === 'text' && !entry.text) entry.text = 'Text';
  }, true), { choices: ['value', 'text', 'local-clock'] });
  propertyField(widgetFields, 'Label', widget.label, value => changeWidget('label', value));
  if (widget.type === 'value') {
    propertyField(widgetFields, 'Widget source', widget.source, value => changeWidget('source', value),
      { choices: [{ value: '', label: 'Select source' }, ...available.map(source => ({ value: source.id, label: `${source.id} · ${source.path}` }))] });
    if (available.findLast(source => source.id === widget.source)?.path === 'navigation.position') {
      propertyField(widgetFields, 'Position format', widget.position_format ?? 'DDM', value => changeWidget('position_format', value),
        { choices: ['DD', 'DDM', 'DMS'] });
    }
    propertyField(widgetFields, 'Display unit', widget.display_unit, value => changeWidget('display_unit', value),
      { choices: ['', 'm', 'km', 'ft', 'm/s', 'kn', 'km/h', 'rad', 'deg', 'K', 'C', 'F', 'Pa', 'hPa', 'bar', 'psi', 'Hz', 'rpm', 'V', 'A', '%', 's', 'min', 'h'] });
    propertyField(widgetFields, 'Decimals', widget.decimals ?? 0, value => changeWidget('decimals', value), { number: true, max: 6 });
    propertyField(widgetFields, 'Maximum digits', widget.max_digits ?? 6, value => changeWidget('max_digits', value), { number: true, min: 1, max: 32 });
  } else if (widget.type === 'text') propertyField(widgetFields, 'Text', widget.text, value => changeWidget('text', value));
  for (const [key, label] of [['column', 'Column'], ['row', 'Row'], ['column_span', 'Column span'], ['row_span', 'Row span']]) {
    propertyField(widgetFields, label, widget.cell[key] ?? (key.endsWith('span') ? 1 : 0), value => editDocument(document => {
      if (value === undefined) throw new Error(`${label} is required`);
      const target = currentPage(document); const entry = target.widgets[editorWidget]; entry.cell[key] = value;
      if (!widgetFits(target, entry, editorWidget)) throw new Error('Widget must fit the grid without overlapping another widget');
    }, true), { number: true, min: key.endsWith('span') ? 1 : 0, max: 255 });
  }
  widgetFields.append(icon('trash-2', 'Delete widget', () => editDocument(document => { currentPage(document).widgets.splice(editorWidget, 1); editorWidget = 0; }, true)));
  container.append(widgetFields);
}

function renderProperties() {
  const container = element('properties'); container.replaceChildren();
  let configuration;
  try { configuration = JSON.parse(element('json').value); }
  catch { container.append(text('p', 'Invalid JSON', 'error-text')); return; }
  const general = text('div', '', 'property-grid');
  container.append(text('h2', 'Configuration', 'configuration-heading'));
  propertyField(general, 'Configuration name', configuration.name, value => editDocument(document => { document.name = value; }));
  container.append(general);
  container.append(text('p', `History RAM: ${historyBytes(configuration)} / 1024 bytes`, 'device-meta'));
  renderSources(container, configuration, { title: 'Permanent sources', address: null, sources: configuration.permanent_sources ?? [] });
  renderLayout(container, configuration);
  if (typeof lucide !== 'undefined') lucide.createIcons();
}

function renderSources(container, configuration, group) {
    const key = group.address === null ? 'permanent' : `section-${group.address}`;
    const block = document.createElement('details'); block.className = 'source-panel';
    block.open = openSourcePanels.has(key);
    block.ontoggle = () => { if (block.isConnected) { if (block.open) openSourcePanels.add(key); else openSourcePanels.delete(key); } };
    block.append(text('summary', `${group.title} (${group.sources.length})`));
    const heading = text('div', '', 'property-heading');
    const getSources = document => group.address === null ? (document.permanent_sources ??= []) : (document.sections[group.address].sources ??= []);
    heading.append(icon('plus', `Add source to ${group.title}`, () => editDocument(document => {
      const sources = getSources(document); let suffix = sources.length + 1;
      while (sources.some(source => source.id === `source_${suffix}`)) suffix += 1;
      sources.push({ id: `source_${suffix}`, provider: 'signalk', path: signalPaths[0]?.path ?? '', stale_ms: 10000 });
    }, true)));
    block.append(heading);
    for (const [index, source] of group.sources.entries()) {
      const row = text('div', '', 'source-properties');
      const change = (key, value) => editDocument(document => { getSources(document)[index][key] = value; });
      propertyField(row, 'Source ID', source.id, value => editDocument(document => {
        if (!value || getSources(document).some((entry, position) => position !== index && entry.id === value)) throw new Error('Source IDs must be non-empty and unique');
        getSources(document)[index].id = value;
        const sections = group.address === null ? document.sections.filter(section => !section.sources?.some(entry => entry.id === source.id)) : [document.sections[group.address]];
        for (const section of sections) for (const page of section.pages ?? []) {
          if (page.history_plot?.source === source.id) page.history_plot.source = value;
          for (const widget of page.widgets ?? []) if (widget.source === source.id) widget.source = value;
        }
      }, true));
      const signalKSource = source.provider === undefined || ['signalk', 'signalk-text', 'signalk-ugm3'].includes(source.provider);
      const path = propertyField(row, 'Signal K path', source.path, value => editDocument(document => {
        const entry = getSources(document)[index]; entry.path = value;
        const known = signalPaths.find(candidate => candidate.path === value);
        if (entry.provider === 'signalk-ugm3') entry.unit = 'ug/m3';
        else if (known) { if (known.unit) entry.unit = known.unit; else delete entry.unit; }
      }, true), { path: signalKSource });
      path.disabled = !signalKSource;
      propertyField(row, 'Unit', source.unit, value => change('unit', value));
      const changeAge = (key, value) => editDocument(document => {
        const entry = getSources(document)[index]; entry[key] = value;
        if (entry.outdated_ms && entry.stale_ms && entry.outdated_ms >= entry.stale_ms) {
          throw new Error('Outdated after must be less than Stale after');
        }
      });
      propertyField(row, 'Outdated after (ms)', source.outdated_ms, value => changeAge('outdated_ms', value), { number: true });
      propertyField(row, 'Stale after (ms)', source.stale_ms, value => changeAge('stale_ms', value), { number: true });
      propertyField(row, 'Update period (ms)', source.subscribe?.period_ms, value => editDocument(document => {
        (getSources(document)[index].subscribe ??= {}).period_ms = value;
      }), { number: true });
      const sections = group.address === null ? configuration.sections.filter(section => !section.sources?.some(entry => entry.id === source.id)) : [configuration.sections[group.address]];
      const usedByPlot = sections.some(section => section.pages?.some(page => page.history_plot?.source === source.id));
      const recording = propertyField(row, `Record history ${source.id}`, Boolean(source.history), value => editDocument(document => {
        const target = getSources(document)[index];
        if (value) target.history = { interval_ms: 60000, points: 120, reducer: 'mean' };
        else { if (usedByPlot) throw new Error('History is assigned to a page plot'); delete target.history; }
        if (historyBytes(document) > 1024) throw new Error('History exceeds 1024 bytes');
      }, true), { checkbox: true });
      recording.disabled = source.provider === 'signalk-text';
      if (source.history) {
        for (const [key, label, maximum] of [['interval_ms', 'History interval (ms)', 4294967295], ['points', 'History points', 248]])
          propertyField(row, label, source.history[key], value => editDocument(document => {
            if (!Number.isInteger(value) || value < 1) throw new Error(`${label} must be positive`);
            getSources(document)[index].history[key] = value;
            if (historyBytes(document) > 1024) throw new Error('History exceeds 1024 bytes');
          }, true), { number: true, min: 1, max: maximum });
        propertyField(row, 'History reducer', source.history.reducer ?? 'last', value => editDocument(document => {
          getSources(document)[index].history.reducer = value;
        }), { choices: ['last', 'mean', 'minimum', 'maximum'] });
      }
      const remove = icon('trash-2', `Delete source ${source.id}`, () => editDocument(document => { getSources(document).splice(index, 1); }, true));
      remove.disabled = usedByPlot || sections.some(section => section.pages?.some(page => page.widgets?.some(widget => widget.source === source.id)));
      if (remove.disabled) remove.title = 'Source is used by a widget or history plot';
      row.append(remove); block.append(row);
    }
    container.append(block);
}

function editorMode(properties) {
  if (properties) renderProperties();
  element('properties').hidden = !properties; element('json').hidden = properties;
  for (const [id, active] of [['properties-mode', properties], ['json-mode', !properties]]) {
    element(id).classList.toggle('active', active); element(id).setAttribute('aria-selected', String(active));
  }
}

element('properties-mode').onclick = () => editorMode(true);
element('json-mode').onclick = () => editorMode(false);
element('refresh-paths').onclick = async () => {
  try { await refreshPaths(); renderProperties(); }
  catch (error) { notice(error.message, true); }
};

function canDiscard() {
  return !element('json').value || element('editor-status').textContent === 'Saved' || confirm('Discard unsaved configuration changes?');
}

function renderCatalog() {
  const container = element('catalog-list'); container.replaceChildren();
  for (const name of names) {
    const button = text('button', name, selectedName === name ? 'selected' : '');
    button.onclick = async () => {
      if (!canDiscard()) return;
      try { setDocument(name, await api(`configurations/${encodeURIComponent(name)}`), true); }
      catch (error) { notice(error.message, true); }
    };
    container.append(button);
  }
  if (!names.length) container.append(text('p', 'Empty catalog', 'empty'));
}

async function refreshCatalog() {
  const replacement = (await api('configurations')).names;
  if (JSON.stringify(replacement) === JSON.stringify(names)) return;
  names = replacement;
  renderCatalog();
}

for (const button of document.querySelectorAll('nav .tab')) button.onclick = () => {
  for (const tab of document.querySelectorAll('nav .tab')) { tab.classList.toggle('active', tab === button); tab.setAttribute('aria-selected', String(tab === button)); }
  element('devices-view').hidden = button.dataset.view !== 'devices';
  element('designer-view').hidden = button.dataset.view !== 'designer';
};

element('json').oninput = updateEditorStatus;
element('filename').oninput = updateEditorStatus;
element('new').onclick = () => {
  let filename = 'new-display.json'; let suffix = 2;
  while (names.includes(filename)) filename = `new-display-${suffix++}.json`;
  element('new-filename').value = filename;
  element('new-config-dialog').showModal();
  element('new-filename').focus();
};
element('cancel-new').onclick = () => element('new-config-dialog').close();
element('new-config-form').onsubmit = event => {
  event.preventDefault();
  const filename = element('new-filename').value;
  if (!/^[A-Za-z0-9][A-Za-z0-9_.-]*\.json$/.test(filename) || filename.length > 47 || names.includes(filename)) {
    element('new-filename').setCustomValidity('Choose a unique JSON filename using letters, numbers, dots, underscores or hyphens');
    element('new-filename').reportValidity(); return;
  }
  if (!canDiscard()) return;
  const document = templates[element('template').value] ?? { schema: 'semantic-display/v1', name: 'New configuration',
    sections: [{ id: 'section_1', title: 'SECTION 1', sources: [], pages: [{ id: 'page_1', title: 'PAGE 1', grid: { columns: 2, rows: 2, gap: 4 }, widgets: [] }] }] };
  setDocument(filename, document, false); element('new-config-dialog').close();
};
element('new-filename').oninput = () => element('new-filename').setCustomValidity('');
element('save').onclick = async () => {
  try {
    const name = element('filename').value;
    if (names.includes(name) && name !== selectedName && !confirm(`Replace ${name}?`)) return;
    const document = JSON.parse(element('json').value);
    await api(`configurations/${encodeURIComponent(name)}`, 'PUT', document);
    setDocument(name, document, true); await refreshCatalog(); notice(`Saved ${name}`);
  } catch (error) { notice(error.message, true); }
};
element('delete').onclick = async () => {
  const name = element('filename').value;
  if (!names.includes(name) || !confirm(`Delete ${name}?`)) return;
  try { await api(`configurations/${encodeURIComponent(name)}`, 'DELETE'); selectedName = null; savedText = ''; updateEditorStatus(); await refreshCatalog(); notice(`Deleted ${name}`); }
  catch (error) { notice(error.message, true); }
};
element('download').onclick = () => {
  try {
    const data = JSON.stringify(JSON.parse(element('json').value), null, 2);
    const url = URL.createObjectURL(new Blob([data], { type: 'application/json' }));
    const anchor = document.createElement('a'); anchor.href = url; anchor.download = element('filename').value; anchor.click(); URL.revokeObjectURL(url);
  } catch (error) { notice(error.message, true); }
};
window.addEventListener('beforeunload', event => { if (element('json').value && element('editor-status').textContent !== 'Saved') { event.preventDefault(); event.returnValue = ''; } });

async function init() {
  if (typeof lucide !== 'undefined') lucide.createIcons();
  try { await refreshCatalog(); }
  catch (error) { notice(error.message, true); }
  try {
    templates = await api('templates');
    for (const name of Object.keys(templates)) { const option = text('option', name); option.value = name; element('template').append(option); }
    if (!element('json').value && names.length) {
      const name = names[0]; const document = await api(`configurations/${encodeURIComponent(name)}`);
      if (!element('json').value) setDocument(name, document, true);
    } else if (!element('json').value && Object.keys(templates).length) setDocument('new-display.json', templates[Object.keys(templates)[0]], false);
  } catch (error) { notice(error.message, true); }
  try { await refreshPaths(); }
  catch (error) { element('path-count').textContent = 'Paths unavailable'; notice(error.message, true); }
  setInterval(async () => {
    try { await refreshCatalog(); }
    catch (error) { notice(error.message, true); }
  }, 5000);
}
void init();