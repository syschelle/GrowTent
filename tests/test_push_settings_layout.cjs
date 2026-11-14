const fs=require('fs'),vm=require('vm'),assert=require('node:assert/strict');const {chromium}=require('playwright');
(async()=>{
 const payload=p=>fs.readFileSync(p,'utf8').split('R"rawliteral(')[1].split(')rawliteral";')[0];
 const js=payload('src/java_script.h'),css=payload('src/style_css.h'),tags={};new vm.Script(js);
 vm.runInNewContext(js.slice(js.indexOf('(function injectI18N(){'),js.indexOf('})();')+5),{document:{createElement:()=>({}),head:{appendChild:el=>tags[el.id]=el}}});const dict=JSON.parse(tags.i18n.textContent);
 const html=payload('src/index_html.h').replace(/%[A-Z_0-9]+%/g,'').replace(/<script\b[^>]*>[\s\S]*?<\/script\s*>/gi,'').replace(/<link\b[^>]*>/gi,'').replace('</title>','</title><style>'+css+'</style>');
 const browser=await chromium.launch({headless:true}),page=await browser.newPage();await page.setContent(html);
 await page.evaluate(()=>{document.querySelectorAll('.page').forEach(el=>el.classList.remove('active'));document.getElementById('message').classList.add('active');});
 assert.equal(await page.locator('#message .push-settings-panel').count(),2);assert.equal(await page.locator('#message details,#message summary').count(),0);
 const structure=await page.evaluate(()=>{
  const form=document.getElementById('messageSettingsForm'),controls=[...document.querySelectorAll('#message input,#message button')];
  return {form:controls.every(el=>el.form===form),labels:controls.filter(el=>el.tagName==='INPUT').every(el=>el.labels.length>0),unique:controls.every(el=>document.querySelectorAll('#'+CSS.escape(el.id)).length===1),action:form.getAttribute('action'),method:form.method,
   masked:['webPushoverUserKey','webPushoverAppKey','webGotifyToken'].every(id=>{const el=document.getElementById(id);return el.type==='password'&&el.autocomplete==='new-password';})};
 });assert.ok(structure.form&&structure.labels&&structure.unique&&structure.masked,JSON.stringify(structure));assert.equal(structure.action,'/savemessagesettings');assert.equal(structure.method,'post');
 fs.mkdirSync('layout-previews',{recursive:true});
 for(const lang of ['de','en'])for(const width of [1640,1000,800,390]){
  await page.setViewportSize({width,height:1000});await page.evaluate(({dict,lang})=>{document.documentElement.lang=lang;document.querySelectorAll('#message [data-i18n]').forEach(el=>{const entry=dict[el.dataset.i18n]?.[lang];if(entry!==undefined){if(el.dataset.i18nAttr)el.setAttribute(el.dataset.i18nAttr,entry);else el.textContent=entry;}});scrollTo(0,0);},{dict,lang});await page.waitForTimeout(350);
  assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1),'Page overflow '+lang+' '+width);
  assert.ok(await page.evaluate(()=>[...document.querySelectorAll('#message .runsettings-panel')].every(el=>el.scrollWidth<=el.clientWidth+1)),'Panel overflow '+lang+' '+width);
  for(const input of await page.locator('#message input').all())assert.ok(await input.isVisible(),'Provider fields are permanently visible');
  const geometry=await page.locator('[data-push-provider="pushover"] .push-settings-field').evaluateAll(els=>els.map(el=>{const r=el.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width};}));
  if(width>=1000)assert.ok(geometry[1].x>geometry[0].x&&Math.abs(geometry[1].y-geometry[0].y)<2,'Desktop fields share one row');
  else assert.ok(geometry.every(r=>Math.abs(r.x-geometry[0].x)<2),'Narrow display fields stack');
  await page.screenshot({path:`layout-previews/push-${lang}-${width}.png`,fullPage:true});
  console.log(`Push ${lang} ${width}px: permanent sections, aligned fields and no overflow`);
 }
 // Execute the production submit listener with real browser FormData and synthetic transport.
 await page.evaluate(()=>{
  window._pushRequests=[];window._pushMessages=[];window._pushFail=false;
  window.trKey=key=>key;window.showStatusHint=message=>window._pushMessages.push(message);
  window.fetch=async(url,options={})=>{window._pushRequests.push({url,body:options.body?Object.fromEntries(options.body):null});return {ok:true,status:200,json:async()=>url==='/savemessagesettings'?{ok:true,requestId:42}:{ok:true,state:window._pushFail?'failed':'applied',error:window._pushFail?'synthetic_error':undefined}};};
  document.getElementById('webPushoverEnabled').checked=true;document.getElementById('webGotifyEnabled').checked=false;
  const fixture={webPushoverUserKey:'synthetic-user',webPushoverAppKey:'synthetic-app',webPushoverDevice:'test-device',webGotifyURL:'https://example.invalid',webGotifyToken:'synthetic-token'};
  for(const [id,value]of Object.entries(fixture))document.getElementById(id).value=value;
 });
 const start=js.indexOf("  const messageSettingsForm = document.getElementById('messageSettingsForm');"),end=js.indexOf('  // ---------- i18n validation ----------',start);assert.ok(start>=0&&end>start);await page.addScriptTag({content:js.slice(start,end)});
 await page.evaluate(()=>document.getElementById('messageSettingsForm').requestSubmit());await page.waitForFunction(()=>window._pushMessages.includes('message.saved'));
 const posted=await page.evaluate(()=>window._pushRequests[0]);assert.equal(posted.url,'/savemessagesettings');assert.equal(posted.body.webPushoverEnabled,'on');assert.ok(!('webGotifyEnabled'in posted.body));assert.equal(posted.body.webPushoverUserKey,'synthetic-user');assert.equal(posted.body.webPushoverAppKey,'synthetic-app');assert.equal(posted.body.webPushoverDevice,'test-device');assert.equal(posted.body.webGotifyURL,'https://example.invalid');assert.equal(posted.body.webGotifyToken,'synthetic-token');
 assert.ok(await page.evaluate(()=>window._pushRequests.some(r=>r.url==='/api/settings/message-status?requestId=42')),'Waits for confirmed application');
 await page.evaluate(()=>{window._pushFail=true;document.getElementById('saveMessageBtn').disabled=false;document.getElementById('messageSettingsForm').requestSubmit();});await page.waitForFunction(()=>window._pushMessages.includes('message.saveError'));assert.equal(await page.locator('#saveMessageBtn').isDisabled(),false);
 console.log('Production push submit: provider fields/flags, queued confirmation and save-failure recovery passed (no live notifications)');
 await browser.close();
})().catch(error=>{console.error(error);process.exit(1)});
