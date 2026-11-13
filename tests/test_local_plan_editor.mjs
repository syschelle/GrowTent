import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const raw = fs.readFileSync('src/java_script.h', 'utf8');
const js = raw.split('R"rawliteral(')[1].split(')rawliteral";')[0];
new vm.Script(js);
const nodes = {};
vm.runInNewContext(js.slice(js.indexOf('(function injectI18N(){'), js.indexOf('})();')+5), {
  document: {createElement: () => ({}), head: {appendChild: el => {nodes[el.id] = el;}}}
});
const dict = JSON.parse(nodes.i18n.textContent);
const elements = Object.fromEntries(['localPlanEditor','localPlanEnabled','localPlanDays','localPlanDelay','localPlanLast','localPlanSaveStatus'].map(id => [id, {dataset:{}, value:'', checked:false}]));
const requests = [];
let replies = [];
class FixedDate extends Date { constructor(...args) { super(...(args.length ? args : ['2026-11-13T12:00:00Z'])); } }
const ctx = vm.createContext({Date: FixedDate, document: {getElementById: id => elements[id]}, currentLang: 'de',
  trKey: (key, fallback) => dict[key]?.[ctx.currentLang] || fallback,
  setTimeout: fn => {fn();}, fetch: async (url, opts) => {
    requests.push({url,opts});
    const response = replies.shift(); assert.ok(response, 'Unexpected request');
    if (response instanceof Error) throw response;
    return {ok: response.ok, json: async () => response.body};
  }});
vm.runInContext(js.slice(js.indexOf('  // ---------- Local plan editor ----------'), js.indexOf('  const localPlanEditor =')), ctx);
function render(revision, last = null) {
  ctx.data = {'irrigation.plan.revision': revision, 'irrigation.plan.enabled': true, 'irrigation.plan.everyNDays': 3,
    'irrigation.plan.offsetMinutes': 90, 'irrigation.plan.lastRunDate': last};
  vm.runInContext('renderLocalPlanEditor(data)', ctx);
}
render(5);
assert.equal(elements.localPlanEditor.dataset.revision, 5);
assert.equal(elements.localPlanDays.value, 3);
assert.equal(elements.localPlanDelay.value, 90);
assert.equal(elements.localPlanLast.value, '', 'Unknown history must not fabricate a date');
vm.runInContext('localPlanDirty = true', ctx);
elements.localPlanDays.value = '8';
render(6, '2026-11-13');
assert.equal(elements.localPlanDays.value, '8', 'Polling must not overwrite edits');
assert.equal(elements.localPlanEditor.dataset.revision, 5, 'Preserve editing base for conflict detection');
assert.equal(elements.localPlanLast.value, '', 'Polling preserves an edited date');
assert.equal(elements.localPlanLast.min, '2026-11-13');
elements.localPlanLast.value = '2026-11-13';
replies = [{ok: false, body: {ok:false, error:'revision_conflict'}}];
await vm.runInContext('saveLocalPlan()', ctx);
assert.equal(elements.localPlanSaveStatus.dataset.messageKey, 'plan.conflict');
assert.equal(JSON.parse(requests.at(-1).opts.body).expected_revision, 5);
assert.equal(elements.localPlanDays.value, '8');
vm.runInContext('localPlanDirty = false', ctx); render(6);
elements.localPlanDays.value = '0';
const count = requests.length; await vm.runInContext('saveLocalPlan()', ctx);
assert.equal(requests.length, count, 'Invalid interval must not be posted');
elements.localPlanDays.value = '3'; elements.localPlanDelay.value = '1441';
await vm.runInContext('saveLocalPlan()', ctx); assert.equal(requests.length, count);
elements.localPlanDelay.value = '120'; vm.runInContext('localPlanDirty = true', ctx);
replies = [{ok:true, body:{ok:true, applyQueued:true, requestId:42}},
  {ok:true, body:{revision:7, enabled:true, every_n_days:3, offset_after_light_on_min:120, last_run_date:'2026-11-13',last_applied_request:42,pending_request:0}}];
await vm.runInContext('saveLocalPlan()', ctx);
assert.equal(elements.localPlanSaveStatus.dataset.messageKey, 'plan.saved');
assert.equal(elements.localPlanEditor.dataset.revision, 7);
assert.equal(elements.localPlanEditor.disabled, false);
assert.equal(JSON.parse(requests.at(-2).opts.body).last_run_date, null, 'An unknown date stays null');
assert.equal(elements.localPlanLast.value, '2026-11-13');
render(6); assert.equal(elements.localPlanEditor.dataset.revision, 7, 'Ignore stale state');
ctx.currentLang = 'en'; render(7);
assert.equal(elements.localPlanLast.value, '');
// Date safety: no future date, no clearing/rewinding already stored history.
render(8, '2026-11-12');
for (const date of ['', '2026-11-11', '2026-11-14']) {
  elements.localPlanLast.value = date;
  const before = requests.length;
  await vm.runInContext('saveLocalPlan()', ctx);
  assert.equal(requests.length, before);
  assert.equal(elements.localPlanSaveStatus.dataset.messageKey, 'plan.dateInvalid');
}
elements.localPlanLast.value = '2026-11-12';
replies = [new Error('offline')]; await vm.runInContext('saveLocalPlan()', ctx);
assert.equal(elements.localPlanSaveStatus.dataset.messageKey, 'plan.failed');
assert.equal(elements.localPlanEditor.disabled, false);
const html = fs.readFileSync('src/index_html.h','utf8');
assert.ok(html.indexOf('id="irrigationSettingsSection"') < html.indexOf('<div class="runsettings-operations-row">'));
assert.ok(html.includes('<input id="localPlanLast" type="date"'));
const impl = fs.readFileSync('src/irrigation_plan_impl.h','utf8');
const local = impl.slice(impl.indexOf('void lpHandleLocal('),impl.indexOf('bool lpParseLight('));
assert.ok(!local.includes('lpPersist('), 'Async callbacks must never write NVS');
for (const token of ['expected_revision','UINT32_MAX','revision_conflict','lpOp!=PlanOp::None','lpPendingLocal=true','application/json','watering_running']) assert.ok(local.includes(token));
assert.ok(impl.includes('lpPendingLocal=false;lpPendingKey=key'), 'HMAC writes must not inherit local authorization');
assert.ok(impl.includes('if(!lpPendingLocal){'), 'Local edits must preserve history');
assert.ok(impl.includes('same journal/queue as HMAC'));
assert.ok(impl.match(/historyDateAllowed/g).length >= 2, 'Date guards must apply at reception and main-loop apply');
console.log('Local plan editor: date import and history protection, dirty state, revision conflicts, bounds, save confirmation, language and failure tests passed.');
