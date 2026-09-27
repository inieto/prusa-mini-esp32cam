// PrusaCam byClaude web UI. No frameworks; device data is only ever rendered via textContent.

const API = 'api/v1';

// ---------- tiny DOM helpers ----------
const $ = (sel) => document.querySelector(sel);

function h(tag, props = {}, ...children) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(props)) {
    if (v === undefined || v === null || v === false) continue;
    if (k === 'class') el.className = v;
    else if (k === 'text') el.textContent = v;
    else if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
    else if (k in el) el[k] = v;
    else el.setAttribute(k, v === true ? '' : v);
  }
  for (const c of children.flat()) if (c != null) el.append(c instanceof Node ? c : String(c));
  return el;
}

function toast(message, isError = false) {
  const t = h('div', { class: `toast${isError ? ' error' : ''}`, role: 'status', text: message });
  document.body.append(t);
  setTimeout(() => t.remove(), 3500);
}

// ---------- API ----------
class Unauthorized extends Error {}

async function api(path, { method = 'GET', body } = {}) {
  const res = await fetch(`${API}/${path}`, {
    method,
    credentials: 'same-origin',
    headers: body !== undefined ? { 'Content-Type': 'application/json' } : {},
    body: body !== undefined ? JSON.stringify(body) : undefined,
  });
  if (res.status === 401) {
    showLogin();
    throw new Unauthorized();
  }
  const type = res.headers.get('Content-Type') || '';
  const data = type.includes('json') ? await res.json() : await res.text();
  if (!res.ok) throw new Error((data && data.error) || `HTTP ${res.status}`);
  return data;
}

// ---------- formatting ----------
function ago(seconds) {
  if (seconds == null) return 'nunca';
  if (seconds < 60) return `hace ${seconds} s`;
  if (seconds < 3600) return `hace ${Math.floor(seconds / 60)} min`;
  return `hace ${Math.floor(seconds / 3600)} h`;
}

function duration(seconds) {
  const d = Math.floor(seconds / 86400);
  const hms = new Date(seconds * 1000).toISOString().substring(11, 19);
  return d ? `${d} d ${hms}` : hms;
}

const STATE_CLASS = {
  online: 'ok', starting: 'warn', 'not configured': 'warn',
  offline: 'bad', 'server error': 'warn', 'request rejected': 'bad', 'token rejected': 'bad',
};

// ---------- declarative settings forms ----------
const RESOLUTIONS = [
  ['qvga', '320×240'], ['cif', '352×288'], ['vga', '640×480'], ['svga', '800×600'],
  ['xga', '1024×768'], ['sxga', '1280×1024'], ['uxga', '1600×1200'],
];
const LEVEL = { type: 'range', min: -2, max: 2, step: 1 };

const FORMS = {
  camera: {
    title: 'Cámara',
    endpoint: 'config/camera',
    sections: [
      ['Imagen', [
        { key: 'resolution', label: 'Resolución', type: 'select', options: RESOLUTIONS },
        { key: 'jpeg_quality', label: 'Calidad JPEG', type: 'range', min: 10, max: 63, step: 1,
          invert: true, hint: 'Más a la derecha = mejor calidad y archivo más grande' },
        { key: 'rotation', label: 'Rotación', type: 'select',
          options: [[0, '0°'], [90, '90°'], [180, '180°'], [270, '270°']] },
        { key: 'hmirror', label: 'Espejo horizontal', type: 'switch' },
        { key: 'vflip', label: 'Volteo vertical', type: 'switch' },
      ]],
      ['Ajustes', [
        { key: 'brightness', label: 'Brillo', ...LEVEL },
        { key: 'contrast', label: 'Contraste', ...LEVEL },
        { key: 'saturation', label: 'Saturación', ...LEVEL },
      ]],
      ['Exposición y ganancia', [
        { key: 'auto_exposure', label: 'Exposición automática', type: 'switch' },
        { key: 'aec_dsp', label: 'Exposición nocturna (AEC2)', type: 'switch',
          hint: 'Mejora escenas con poca luz' },
        { key: 'ae_level', label: 'Nivel de exposición', ...LEVEL },
        { key: 'manual_exposure', label: 'Exposición manual', type: 'range', min: 0, max: 1200, step: 10,
          showIf: (v) => !v.auto_exposure },
        { key: 'auto_gain', label: 'Ganancia automática', type: 'switch' },
        { key: 'gain_ceiling', label: 'Techo de ganancia', type: 'select',
          options: [[0, '2×'], [1, '4×'], [2, '8×'], [3, '16×'], [4, '32×'], [5, '64×'], [6, '128×']],
          hint: 'Más alto = imagen más clara con poca luz, pero con más ruido' },
        { key: 'manual_gain', label: 'Ganancia manual', type: 'range', min: 0, max: 30, step: 1,
          showIf: (v) => !v.auto_gain },
        { key: 'auto_white_balance', label: 'Balance de blancos automático', type: 'switch' },
        { key: 'lens_correction', label: 'Corrección de lente', type: 'switch' },
      ]],
      ['Flash', [
        { key: 'flash_on_capture', label: 'Flash al capturar', type: 'switch' },
        { key: 'flash_lead_ms', label: 'Encendido previo', type: 'range', min: 0, max: 2000, step: 50,
          unit: 'ms', showIf: (v) => v.flash_on_capture },
        { key: 'flash_duty_pct', label: 'Intensidad', type: 'range', min: 5, max: 100, step: 5,
          unit: '%', showIf: (v) => v.flash_on_capture },
      ]],
    ],
  },
  connect: {
    title: 'PrusaConnect',
    endpoint: 'config/connect',
    sections: [
      ['Conexión', [
        { key: 'token', label: 'Token de la cámara', type: 'secret', setKey: 'token_set',
          hint: 'En PrusaConnect: Cámaras → Agregar otra cámara → copiar el token' },
        { key: 'fingerprint', label: 'Fingerprint', type: 'text', readonly: true },
        { key: 'hostname', label: 'Servidor', type: 'text' },
        { key: 'default_interval_s', label: 'Intervalo por defecto', type: 'select',
          options: [[10, '10 s'], [30, '30 s'], [60, '60 s']],
          hint: 'Se reemplaza por el intervalo que elijas en PrusaConnect' },
      ]],
    ],
  },
  network: {
    title: 'Red',
    endpoint: 'config/network',
    sections: [
      ['WiFi', [
        { key: 'ssid', label: 'Red (SSID)', type: 'text' },
        { key: 'password', label: 'Contraseña', type: 'secret', setKey: 'password_set' },
        { key: 'hostname', label: 'Nombre en la red (mDNS)', type: 'text', hint: 'Se accede como http://<nombre>.local' },
      ]],
    ],
    extra: () => wifiScanner(),
  },
};

function fieldControl(field, values, onChange) {
  const value = values[field.key];
  switch (field.type) {
    case 'switch': {
      const input = h('input', { type: 'checkbox', checked: !!value, id: `f-${field.key}` });
      input.addEventListener('change', () => onChange(input.checked));
      return h('label', { class: 'switch' }, input, h('span'));
    }
    case 'range': {
      const toUi = (v) => (field.invert ? field.max + field.min - v : v);
      const input = h('input', { type: 'range', min: field.min, max: field.max, step: field.step,
                                 value: toUi(value), id: `f-${field.key}` });
      const shown = h('span', { class: 'range-value' });
      const paint = () => {
        const pct = ((input.value - field.min) / (field.max - field.min)) * 100;
        input.style.setProperty('--fill', `${pct}%`);
        // Inverted scales (JPEG quality) read better as a percentage than as raw driver values.
        shown.textContent = field.invert
          ? `${Math.round(pct)} %`
          : `${input.value}${field.unit ? ` ${field.unit}` : ''}`;
      };
      input.addEventListener('input', paint);
      input.addEventListener('change', () => onChange(toUi(Number(input.value))));
      paint();
      return [input, shown];
    }
    case 'select': {
      const select = h('select', { id: `f-${field.key}` },
        field.options.map(([v, text]) => h('option', { value: v, text, selected: String(v) === String(value) })));
      select.addEventListener('change', () => {
        const raw = select.value;
        onChange(typeof field.options[0][0] === 'number' ? Number(raw) : raw);
      });
      return select;
    }
    case 'secret': {
      const isSet = !!values[field.setKey];
      const input = h('input', { type: 'password', id: `f-${field.key}`, autocomplete: 'off',
                                 placeholder: isSet ? '•••••••• (guardado)' : 'sin configurar' });
      input.addEventListener('change', () => onChange(input.value));
      const eye = h('img', { src: 'icons/eye.svg', alt: 'mostrar', width: 18, style: 'cursor:pointer' });
      eye.addEventListener('click', () => {
        input.type = input.type === 'password' ? 'text' : 'password';
        eye.src = input.type === 'password' ? 'icons/eye.svg' : 'icons/eye-slash.svg';
      });
      return [input, eye];
    }
    default: {
      const input = h('input', { type: 'text', value: value ?? '', readOnly: !!field.readonly,
                                 id: `f-${field.key}` });
      input.addEventListener('change', () => onChange(input.value));
      return input;
    }
  }
}

async function renderForm(name) {
  const spec = FORMS[name];
  const values = await api(spec.endpoint);
  const changes = {};
  const view = $('#view');

  const draw = () => {
    view.replaceChildren();
    const merged = { ...values, ...changes };
    for (const [heading, fields] of spec.sections) {
      view.append(h('h2', { text: heading }));
      const form = h('div', { class: 'form' });
      for (const field of fields) {
        if (field.showIf && !field.showIf(merged)) continue;
        form.append(
          h('label', { class: 'key', for: `f-${field.key}`, text: field.label }),
          h('div', { class: 'value' }, fieldControl(field, merged, (v) => {
            changes[field.key] = v;
            if (fields.some((f) => f.showIf)) draw();
            save.disabled = false;
          })),
        );
        if (field.hint) form.append(h('div', { class: 'hint', text: field.hint }));
      }
      view.append(form);
    }
    view.append(h('div', { class: 'form-actions' }, save));
    if (spec.extra) view.append(spec.extra());
  };

  const save = h('button', { class: 'btn medium', type: 'button', text: 'Guardar', disabled: true });
  save.addEventListener('click', async () => {
    save.disabled = true;
    try {
      const result = await api(spec.endpoint, { method: 'PUT', body: changes });
      Object.assign(values, result);
      for (const k of Object.keys(changes)) delete changes[k];
      toast(result.reboot_required ? 'Guardado. Se aplica al reiniciar.' : 'Guardado');
      draw();
      refreshStatus();
    } catch (e) {
      if (!(e instanceof Unauthorized)) toast(`No se pudo guardar: ${e.message}`, true);
      save.disabled = false;
    }
  });
  draw();
}

function wifiScanner() {
  const body = h('tbody');
  const button = h('button', { class: 'btn medium', type: 'button', text: 'Buscar redes' });
  button.addEventListener('click', async () => {
    button.disabled = true;
    try {
      const networks = await api('wifi/scan');
      body.replaceChildren(...networks.map((n) => h('tr', {},
        h('td', { text: n.ssid || '(oculta)' }),
        h('td', { text: `${n.rssi} dBm` }),
        h('td', { text: n.channel }),
        h('td', { text: n.auth }),
        h('td', {}, h('button', { class: 'btn small', type: 'button', text: 'Usar',
          onclick: () => { const i = $('#f-ssid'); i.value = n.ssid; i.dispatchEvent(new Event('change')); } })),
      )));
    } catch (e) {
      if (!(e instanceof Unauthorized)) toast(e.message, true);
    }
    button.disabled = false;
  });
  return h('div', {},
    h('h2', { text: 'Redes disponibles' }),
    h('div', { class: 'form-actions' }, button),
    h('table', { class: 'data' },
      h('thead', {}, h('tr', {}, ['SSID', 'Señal', 'Canal', 'Seguridad', ''].map((t) => h('th', { text: t })))),
      body));
}

async function renderSystem() {
  const view = $('#view');
  const s = await api('status');
  const kv = (label, value) => [h('label', { class: 'key', text: label }), h('div', { class: 'value strong', text: value })];
  const logs = h('pre', { class: 'log', text: 'Cargando…' });
  view.replaceChildren(
    h('h2', { text: 'Dispositivo' }),
    h('div', { class: 'form' },
      kv('Firmware', s.version),
      kv('Encendido desde', duration(s.uptime_s)),
      kv('RAM interna libre', `${Math.round(s.heap.internal_free / 1024)} KB (mínimo ${Math.round(s.heap.internal_min / 1024)} KB)`),
      kv('PSRAM libre', `${Math.round(s.heap.psram_free / 1024)} KB`),
      kv('WiFi', `${s.wifi.ssid} · ${s.wifi.ip} · ${s.wifi.rssi} dBm`),
      kv('Subidas OK / fallidas', `${s.connect.uploads_ok} / ${s.connect.uploads_failed}`),
      kv('Último error', s.connect.last_error || '—')),
    h('h2', { text: 'Últimos reinicios' }),
    h('table', { class: 'data' },
      h('thead', {}, h('tr', {}, h('th', { text: 'Motivo' }), h('th', { text: 'Tras' }))),
      h('tbody', {}, (s.resets || []).map((r) => h('tr', {}, h('td', { text: r.reason }), h('td', { text: duration(r.uptime_s) }))))),
    h('h2', { text: 'Log' }),
    logs,
  );
  logs.textContent = await api('logs');
  logs.scrollTop = logs.scrollHeight;
}

// ---------- status header ----------
let lightOn = false;

async function refreshStatus() {
  try {
    const s = await api('status');
    const state = $('#connect-state');
    state.textContent = s.connect.state;
    state.className = `state ${STATE_CLASS[s.connect.state] || ''}`;
    $('#interval').textContent = s.connect.interval_s ? `${s.connect.interval_s} s` : 'manual';
    $('#last-upload').textContent = ago(s.connect.last_upload_ago_s);
    $('#version').textContent = s.version;
    lightOn = !!s.light;
    $('#light-icon').src = lightOn ? 'icons/light-on-icon.svg' : 'icons/light-off-icon.svg';
    const photo = $('#photo');
    if (s.connect.frame_seq !== undefined && photo.dataset.seq !== String(s.connect.frame_seq)) {
      photo.dataset.seq = s.connect.frame_seq;
      photo.src = `${API}/snapshot.jpg?seq=${s.connect.frame_seq}`;
      $('#photo-caption').textContent = `Capturado ${ago(s.connect.frame_age_s)}`;
    }
  } catch (e) {
    if (!(e instanceof Unauthorized)) $('#connect-state').textContent = 'sin conexión con la cámara';
  }
}

// ---------- routing ----------
const ROUTES = { camera: () => renderForm('camera'), connect: () => renderForm('connect'),
                 network: () => renderForm('network'), system: renderSystem };

async function route() {
  const name = (location.hash || '#camera').slice(1);
  for (const a of document.querySelectorAll('#tabs a')) a.classList.toggle('active', a.hash === `#${name}`);
  try {
    await (ROUTES[name] || ROUTES.camera)();
  } catch (e) {
    if (!(e instanceof Unauthorized)) $('#view').replaceChildren(h('p', { text: `Error: ${e.message}` }));
  }
}

// ---------- session ----------
function showLogin() {
  $('#app').hidden = true;
  $('#login').hidden = false;
}

function showApp() {
  $('#login').hidden = true;
  $('#app').hidden = false;
  refreshStatus();
  route();
}

$('#login-form').addEventListener('submit', async (ev) => {
  ev.preventDefault();
  const form = new FormData(ev.target);
  try {
    await api('login', { method: 'POST', body: Object.fromEntries(form) });
    $('#login-error').textContent = '';
    showApp();
  } catch (e) {
    $('#login-error').textContent = e instanceof Unauthorized ? 'Usuario o contraseña incorrectos' : e.message;
  }
});

async function action(path, body, message) {
  try {
    await api(path, { method: 'POST', body: body ?? {} });
    if (message) toast(message);
  } catch (e) {
    if (!(e instanceof Unauthorized)) toast(e.message, true);
  }
}

$('#snap-now').addEventListener('click', async (ev) => {
  ev.target.disabled = true;
  await action('snapshot', {}, 'Snapshot solicitado');
  setTimeout(() => { ev.target.disabled = false; refreshStatus(); }, 2500);
});
$('#light-toggle').addEventListener('click', async () => { await action('light', { on: !lightOn }); refreshStatus(); });
$('#refresh').addEventListener('click', () => { refreshStatus(); route(); });
$('#reboot').addEventListener('click', () => {
  const b = $('#reboot');
  if (b.dataset.armed) {
    action('reboot', {}, 'Reiniciando… la página se reconecta sola.');
    delete b.dataset.armed;
  } else {
    b.dataset.armed = '1';
    b.lastChild.textContent = ' ¿Confirmar?';
    setTimeout(() => { delete b.dataset.armed; b.lastChild.textContent = ' Reiniciar'; }, 4000);
  }
});
$('#logout').addEventListener('click', async () => { await action('logout'); showLogin(); });
$('#photo').addEventListener('click', () => window.open(`${API}/snapshot.jpg`, '_blank'));
window.addEventListener('hashchange', route);

setInterval(() => { if (!$('#app').hidden) refreshStatus(); }, 5000);
showApp();
