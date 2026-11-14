const fs=require('fs'),vm=require('vm'),assert=require('node:assert/strict');const {chromium}=require('playwright');
(async()=>{
 const payload=p=>fs.readFileSync(p,'utf8').split('R"rawliteral(')[1].split(')rawliteral";')[0];
 const js=payload('src/java_script.h'),css=payload('src/style_css.h'),tags={};new vm.Script(js);
 vm.runInNewContext(js.slice(js.indexOf('(function injectI18N(){'),js.indexOf('})();')+5),{document:{createElement:()=>({}),head:{appendChild:el=>tags[el.id]=el}}});const dict=JSON.parse(tags.i18n.textContent);
 const html=payload('src/index_html.h').replace(/%[A-Z_0-9]+%/g,'').replace(/<script\b[^>]*>[\s\S]*?<\/script\s*>/gi,'').replace(/<link\b[^>]*>/gi,'').replace('</title>','</title><style>'+css+'</style>');
 const browser=await chromium.launch({headless:true}),page=await browser.newPage();await page.setContent(html);
 await page.evaluate(()=>{document.querySelectorAll('.page').forEach(el=>el.classList.remove('active'));document.getElementById('settings').classList.add('active');document.querySelectorAll('#settings .relay-extra').forEach(el=>el.style.display='');document.getElementById('language').innerHTML='<option value="de">Deutsch</option><option value="en">English</option>';});
 const s3=await page.locator('#webRelayCount option[value="6"]').count()>0;
 assert.equal(await page.locator('#settings .system-settings-panel').count(),6);assert.equal(await page.locator('#settings details,#settings summary').count(),0);
 assert.equal(await page.locator('#webPowerPriceKwh').evaluate(el=>!!el.closest('.system-device-panel')),true);
 assert.equal(await page.locator('#webDS18B20Name').evaluate(el=>!!el.closest('.system-sensor-panel')),true);
 const link=page.locator('#settings .system-timezone-link');assert.equal(await link.getAttribute('href'),'https://github.com/nayarsystems/posix_tz_db/blob/master/zones.json');assert.equal(await link.getAttribute('target'),'_blank');assert.match(await link.getAttribute('rel'),/noopener/);
 const structure=await page.evaluate(()=>{const f=document.getElementById('systemSettingsForm'),controls=[...document.querySelectorAll('#settings input,#settings select,#settings button')];return {form:controls.every(el=>el.form===f),labels:controls.filter(el=>el.tagName!=='BUTTON').every(el=>el.labels.length>0),unique:controls.every(el=>document.querySelectorAll('#'+CSS.escape(el.id)).length===1),secret:document.getElementById('webBackendWriteSecret').type};});
 assert.ok(structure.form&&structure.labels&&structure.unique,JSON.stringify(structure));assert.equal(structure.secret,'password');
 if(s3){assert.equal(await page.locator('#webRelayCount').isVisible(),false);for(let n=1;n<=3;n++)assert.equal(await page.locator('#webRelayName'+n).count(),0);}
 else assert.equal(await page.locator('#webRelayCount').isVisible(),true);
 fs.mkdirSync('layout-previews',{recursive:true});
 for(const lang of ['de','en'])for(const theme of ['light','dark'])for(const width of [1640,1000,800,390]){
  await page.setViewportSize({width,height:1100});await page.evaluate(({dict,lang,theme})=>{document.documentElement.lang=lang;document.documentElement.dataset.theme=theme;document.querySelectorAll('#settings [data-i18n]').forEach(el=>{const text=dict[el.dataset.i18n]?.[lang];if(text!==undefined){if(el.dataset.i18nAttr)el.setAttribute(el.dataset.i18nAttr,text);else el.textContent=text;}});scrollTo(0,0);},{dict,lang,theme});await page.waitForTimeout(250);
  assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1),'Page overflow '+lang+' '+width);
  assert.ok(await page.evaluate(()=>[...document.querySelectorAll('#settings .system-settings-panel')].every(el=>el.scrollWidth<=el.clientWidth+1)),'Panel overflow '+lang+' '+width);
  for(const panel of await page.locator('#settings .system-settings-panel').all())assert.ok(await panel.isVisible());
  const rect=await page.locator('#settings .system-settings-panel').evaluateAll(els=>els.slice(0,2).map(el=>{const r=el.getBoundingClientRect();return{x:r.x,y:r.y};}));
  if(width>=800)assert.ok(rect[1].x>rect[0].x&&Math.abs(rect[1].y-rect[0].y)<2);else assert.ok(rect[1].y>rect[0].y);
  await page.screenshot({path:`layout-previews/system-${lang}-${theme}-${width}.png`,fullPage:true});
 }
 // Execute the real save handler against the real controls, with synthetic HTTP only.
 await page.evaluate(({s3})=>{
  window._systemPosts=[];window._systemToasts=[];window._systemFail=false;
  window.I18N={};window.isValidIpv4Address=v=>/^192\.0\.2\.\d+$/.test(v);window.showToast=(...args)=>window._systemToasts.push(args);
  window.fetch=async(url,options={})=>{window._systemPosts.push({url,body:options.body?JSON.parse(options.body):null});return {ok:!window._systemFail,status:window._systemFail?409:200,json:async()=>window._systemFail?{ok:false,err:'synthetic_failure'}:url.startsWith('/api/settings/backend-write?')?{lastAppliedId:7}:{ok:true,requestId:7}};};
  window.setTimeout=()=>0;
  const fixture={webRelayCount:s3?'6':'8',webBoxName:'Synthetic tent',webNTPServer:'pool.ntp.org',webTimeZoneInfo:'CET-1CEST,M3.5.0,M10.5.0/3',language:'de',theme:'dark',timeFormat:'24',tempUnit:'C',webPowerPriceKwh:'0.30',webDS18B20Name:'Synthetic water',webBackendWriteAllowedIp:'192.0.2.10',webBackendWriteSecret:''};
  for(let n=s3?4:1;n<=(s3?6:5);n++)fixture['webRelayName'+n]='Relay '+n;
  for(const [id,value]of Object.entries(fixture))document.getElementById(id).value=value;
  document.getElementById('webDebugEnable').checked=true;document.getElementById('webDS18B20Enable').checked=true;document.getElementById('webBackendWriteEnabled').checked=true;
 },{s3});
 const start=js.indexOf('window.saveSystemSettingsAll = async function'),end=js.indexOf('//---------- save Runsettings',start);assert.ok(start>=0&&end>start);await page.addScriptTag({content:js.slice(start,end)});
 await page.evaluate(()=>window.saveSystemSettingsAll());
 const posts=await page.evaluate(()=>window._systemPosts);const body=url=>posts.find(x=>x.url===url)?.body;
 assert.equal(body('/api/settings/active-relay-count').activeRelayCount,s3?6:8);assert.equal(body('/api/settings/power-price').powerPrice,0.3);assert.equal(body('/api/settings/box-name').boxName,'Synthetic tent');assert.equal(body('/api/settings/time-zone').timeZoneInfo,'CET-1CEST,M3.5.0,M10.5.0/3');assert.equal(body('/api/settings/ds18b20-name').ds18b20Name,'Synthetic water');assert.equal(body('/api/settings/backend-write').secret,'');
 assert.equal(posts.at(-1).url,'/api/system/restart');assert.ok(posts.some(p=>p.url.startsWith('/api/settings/backend-write?')));
 for(let n=s3?4:1;n<=(s3?6:5);n++)assert.equal(body('/api/settings/relay-name/'+n).relayName,'Relay '+n);
 if(s3)assert.ok(!posts.some(p=>/relay-name\/[123]$/.test(p.url)));
 await page.evaluate(()=>{document.getElementById('saveSettingsBtn').disabled=false;window._systemFail=true;window._systemPosts=[];return window.saveSystemSettingsAll();});
 assert.equal(await page.locator('#saveSettingsBtn').isDisabled(),false);assert.ok(await page.evaluate(()=>!window._systemPosts.some(x=>x.url==='/api/system/restart')));
 console.log('System settings: DE/EN light/dark desktop/mobile layout, field ownership, timezone link, board controls and real save/ACK/failure handling passed');
 await browser.close();
})().catch(error=>{console.error(error);process.exit(1)});
