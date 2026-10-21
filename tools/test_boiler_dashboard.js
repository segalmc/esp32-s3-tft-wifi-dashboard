const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const page = fs.readFileSync('src/BoilerDashboardPage.h', 'utf8');
const match = page.match(/<script>\n([\s\S]*?)\n<\/script>/);
assert(match, 'dashboard script must be present');

function fixture() {
  return {
    relay: 'OFF', lastConfirmedRelay: 'OFF', connected: true,
    lastConfirmedAgeMs: 1000, fresh: true, leaseActive: false,
    progress: 'confirmed', result: 'OFF verified', commandId: 1, busy: false,
    monitorArmed: true, readGate: true, controlGate: true,
    available: {refresh: true, reconnect: true, on: true, off: true},
    diagnostics: {sessions: 2, pushEvents: 3, heartbeatFailures: 0,
      connectionErrors: 0, authenticationErrors: 0, invalidResponses: 0,
      lastCommand: 16, lastResponseType: 'status', lastFrameLength: 88,
      lastFailure: 'none', reconcileIntervalMs: 30000},
    device: {ssid: 'Kitchen', ip: '192.168.31.230', rssiDbm: -50,
      uptimeMs: 2000, freeHeapBytes: 100000, freePsramBytes: 1000000,
      totalPsramBytes: 8000000, clockSynced: true, otaPhase: 'idle'}
  };
}

async function dashboard(status, approved = true, failAction = false) {
  const elements = new Map();
  const requests = [];
  const context = {
    document: {getElementById(id) {
      if (!elements.has(id)) elements.set(id, {textContent: '', disabled: false,
        className: '', onclick: null});
      return elements.get(id);
    }},
    location: {href: ''},
    confirm: () => approved,
    setInterval: () => 0,
    setTimeout: fn => fn(),
    URLSearchParams,
    fetch: async (url, options = {}) => {
      if (url === '/api/status') return {ok: true, status: 200, json: async () => status};
      if (url === '/logout') return {ok: true, status: 200};
      assert.equal(url, '/api/command');
      assert.equal(options.method, 'POST');
      assert.equal(options.headers['X-CSRF-Token'], '{{CSRF}}');
      const action = options.body.get('action');
      requests.push(action);
      if (failAction) return {ok: false, status: 503, json: async () =>
        ({message: 'USB gate disabled'})};
      status.commandId++;
      return {ok: true, status: 202, json: async () =>
        ({message: 'queued for verification', commandId: status.commandId})};
    }
  };
  vm.createContext(context);
  vm.runInContext(match[1], context);
  await new Promise(resolve => setImmediate(resolve));
  return {elements, requests, context};
}

(async () => {
  const live = await dashboard(fixture());
  assert.equal(live.elements.get('relay').textContent, 'OFF');
  assert.equal(live.elements.get('on').disabled, false);
  await live.elements.get('refresh').onclick();
  await live.elements.get('reconnect').onclick();
  await live.elements.get('on').onclick();
  await live.elements.get('off').onclick();
  assert.deepEqual(live.requests, ['refresh', 'reconnect', 'on', 'off']);
  await live.elements.get('logout').onclick();
  assert.equal(live.context.location.href, '/');

  const cancelled = await dashboard(fixture(), false);
  await cancelled.elements.get('on').onclick();
  assert.deepEqual(cancelled.requests, []);

  const lockedStatus = fixture();
  lockedStatus.relay = 'STALE';
  lockedStatus.connected = false;
  lockedStatus.controlGate = false;
  lockedStatus.available = {refresh: true, reconnect: true, on: false, off: false};
  const locked = await dashboard(lockedStatus);
  assert.equal(locked.elements.get('relay').textContent, 'STALE');
  assert.equal(locked.elements.get('on').disabled, true);
  assert.equal(locked.elements.get('off').disabled, true);
  assert.equal(locked.elements.get('refresh').disabled, false);
  const rejected = await dashboard(fixture(), true, true);
  await rejected.elements.get('on').onclick();
  assert.equal(rejected.elements.get('progress').textContent, 'USB gate disabled');
  await rejected.context.refresh();
  assert.equal(rejected.elements.get('progress').textContent, 'USB gate disabled');
  console.log('Boiler dashboard browser-script tests passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
