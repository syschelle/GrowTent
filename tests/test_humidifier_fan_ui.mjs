import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const raw=fs.readFileSync('src/java_script.h','utf8');
const js=raw.split('R"rawliteral(')[1].split(')rawliteral";')[0];
new vm.Script(js);
const source=js.slice(js.indexOf('// === Humidifier fan editor ==='),js.indexOf('// === End humidifier fan editor ==='));
const nodes=Object.fromEntries(['humidifierFanRelay','humidifierFanSave','humidifierFanState','humidifierFanMessage'].map(id=>[id,{value:'0',disabled:false,textContent:'',handlers:{},addEventListener(type,fn){this.handlers[type]=fn;},replaceChildren(...children){this.children=children;}}]));
const rows=[1,2,3,4,5,6].map(n=>({id:`espRelay${n}Row`,dataset:{},controls:[{disabled:false,checked:true,value:'17'},{disabled:true}],note:null,
 classList:{toggle(){}},setAttribute(){},querySelectorAll(){return this.controls;},querySelector(){return this.note;},
 appendChild(note){this.note=note;note.remove=()=>{this.note=null;};}
}));
let next=[],requests=[],periodic,language;
const document={readyState:'complete',hidden:false,documentElement:{lang:'de'},getElementById:id=>nodes[id],createElement:()=>({}),querySelectorAll:()=>rows};
const context=vm.createContext({document,localStorage:{getItem:()=>null},MutationObserver:class{constructor(fn){language=fn;}observe(){}},setInterval:fn=>{periodic=fn;},setTimeout:fn=>fn(),fetch:async(url,options)=>{
 requests.push({url,options});const reply=next.shift();assert.ok(reply,'Unexpected HTTP request');
 if(reply instanceof Error)throw reply;return {ok:reply.ok??true,json:async()=>reply};
}});
const state=(extra={})=>({ok:true,relay:4,availableRelays:[4,5,6],known:true,autoOffSeconds:60,running:false,stale:false,appliedId:0,failedId:0,...extra});
const flush=()=>new Promise(resolve=>setImmediate(resolve));
next.push(state());vm.runInContext(source,context);await flush();
assert.equal(rows[3].controls[0].disabled,true);assert.equal(rows[2].controls[0].disabled,false);assert.match(rows[3].note.textContent,/Zeitsteuerung pausiert/);
assert.equal(nodes.humidifierFanRelay.value,'4');assert.match(nodes.humidifierFanState.textContent,/60 s.*Fan aus/);
assert.equal(requests[0].url,'/api/settings/humidifier-fan');
assert.deepEqual(nodes.humidifierFanRelay.children.map(o=>o.value),['0','4','5','6']);
document.documentElement.lang='en';language();assert.match(rows[3].note.textContent,/schedule paused/);assert.match(nodes.humidifierFanState.textContent,/Fan off/);
next.push(state({running:true,remainingSeconds:42,stale:true}));periodic();await flush();
assert.match(nodes.humidifierFanState.textContent,/42 s.*Refresh failed/);
nodes.humidifierFanRelay.value='5';nodes.humidifierFanRelay.handlers.change();assert.equal(rows[3].controls[0].disabled,true);assert.equal(rows[4].controls[0].disabled,false);
next.push(state());periodic();await flush();assert.equal(nodes.humidifierFanRelay.value,'5');
next.push({ok:true,requestId:7},state({relay:5,appliedId:7}));await nodes.humidifierFanSave.handlers.click();
assert.equal(JSON.parse(requests.find(r=>r.options.method==='POST').options.body).relay,5);
assert.equal(nodes.humidifierFanMessage.textContent,'Coupling saved.');
assert.equal(rows[3].controls[0].disabled,false);assert.equal(rows[3].controls[1].disabled,true);assert.equal(rows[3].controls[0].checked,true);assert.equal(rows[3].controls[0].value,'17');assert.equal(rows[3].note,null);assert.equal(rows[4].controls[0].disabled,true);
next.push(state({relay:5,known:false}));periodic();await flush();assert.match(nodes.humidifierFanState.textContent,/unknown.*fan off/);
next.push(state({relay:5,autoOffSeconds:0}));periodic();await flush();assert.match(nodes.humidifierFanState.textContent,/disabled.*fan off/);
next.push({ok:true,requestId:8},state({failedId:8}));await nodes.humidifierFanSave.handlers.click();assert.match(nodes.humidifierFanMessage.textContent,/Save not confirmed/);
next.push(new Error('network'));periodic();await flush();assert.equal(nodes.humidifierFanState.textContent,'Status unavailable');
assert.equal(nodes.humidifierFanSave.disabled,false);
assert.ok(requests.every(r=>r.url==='/api/settings/humidifier-fan'),'UI never polls Shelly');
next.push(state({relay:0}));periodic();await flush();assert.equal(rows[4].controls[0].disabled,false);assert.equal(rows[4].note,null);
assert.ok(js.includes("dataset.humidifierFanOwned==='true')continue;"),'Global save skips owned schedule');
console.log('Humidifier fan UI runtime checks passed');
