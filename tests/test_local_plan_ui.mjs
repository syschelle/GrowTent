import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const source = fs.readFileSync('src/java_script.h', 'utf8');
const js = source.split('R"rawliteral(')[1].split(')rawliteral";')[0];
new vm.Script(js); // Parse the entire shipped payload.
const nodes = {};
vm.runInNewContext(js.slice(js.indexOf('(function injectI18N(){'), js.indexOf('})();') + 5), {
  document: {createElement: () => ({}), head: {appendChild: n => {nodes[n.id] = n;}}}
});
const dictionary = JSON.parse(nodes.i18n.textContent);
const el = {textContent: ''};
const context = vm.createContext({window: {}, document: {getElementById: () => el}, currentLang: 'de',
  trKey: (key, fallback) => dictionary[key]?.[context.currentLang] || fallback});
const renderer = js.slice(js.indexOf('  // ---------- Local irrigation plan status'), js.indexOf('  // ---------- Sensor fetch'));
vm.runInContext(renderer, context);
const render = (data, unavailable = false) => {context.data = data; context.failed = unavailable;
  vm.runInContext('renderLocalIrrigationPlanStatus(data, failed)', context); return el.textContent;};
assert.equal(render(null), 'Lokaler Plan: Status wird geladen …');
assert.equal(render({'irrigation.plan.status': 'disabled'}), 'Lokaler Plan: Deaktiviert');
assert.equal(render({}), 'Lokaler Plan: Status nicht verfügbar');
assert.equal(render({'irrigation.plan.status': 'future_status'}), 'Lokaler Plan: Status nicht verfügbar');
assert.equal(render({'irrigation.plan.enabled': false}), 'Lokaler Plan: Deaktiviert');
for (const state of ['drying_blocked','storage_error','event_queue_full','waiting_for_shelly','stale_shelly','ready']) {
  const text = render({'irrigation.plan.status': state, 'irrigation.plan.queuedEvents': 3});
  assert.ok(text.startsWith(`Lokaler Plan: ${dictionary[`status.localPlan.${state}`].de}`));
  assert.ok(text.includes('noch nicht synchronisiert'));
  assert.ok(text.includes('Ereignisse: 3'));
}
assert.ok(!render({'irrigation.plan.status': 'disabled'}).includes('Shelly'));
assert.ok(render({'irrigation.plan.status': 'ready', 'irrigation.plan.lastScheduleSync': 1794564000}).includes('2026'));
assert.equal(render({'irrigation.plan.status': 'ready'}, true), 'Lokaler Plan: Status nicht verfügbar');
context.currentLang = 'en';
assert.equal(render(null), 'Local plan: loading …');
assert.equal(render({'irrigation.plan.status': 'disabled'}), 'Local plan: Disabled');
assert.ok(render({'irrigation.plan.status': 'ready'}).startsWith('Local plan: Ready'));
assert.equal(render({}, true), 'Local plan: Status unavailable');
// Ensure production hooks use this renderer for fetch, errors and language changes.
assert.ok(js.includes('document.documentElement.lang = currentLang;'));
assert.ok(js.includes('renderLocalIrrigationPlanStatus(window._lastApiState || null);'));
assert.ok(js.includes('window._localPlanFetchFailed = false;\n      renderLocalIrrigationPlanStatus(data);'));
assert.ok(js.includes('window._localPlanFetchFailed = true;\n    renderLocalIrrigationPlanStatus(window._lastApiState || null, true);'));
const html = fs.readFileSync('src/index_html.h', 'utf8');
for (const asset of ['script.js', 'style.css']) assert.ok(html.includes(`/${asset}?v=%FWVERSION%`));
assert.ok(fs.readFileSync('src/runtime.h', 'utf8').includes('html.replace("%FWVERSION%", String(FW_VERSION))'));
assert.ok(fs.readFileSync('src/main.cpp', 'utf8').includes('request->hasParam("v") && request->getParam("v")->value() == FW_VERSION'));
console.log('Local plan UI: production JS syntax, DE/EN states, failure handling and cache regression checks passed.');
