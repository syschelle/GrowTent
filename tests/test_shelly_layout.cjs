const fs=require('fs');const vm=require('vm');const assert=require('node:assert/strict');const {chromium}=require('playwright');
(async()=>{
 const payload=p=>fs.readFileSync(p,'utf8').split('R"rawliteral(')[1].split(')rawliteral";')[0];
 const js=payload('src/java_script.h'),css=payload('src/style_css.h');const tags={};
 vm.runInNewContext(js.slice(js.indexOf('(function injectI18N(){'),js.indexOf('})();')+5),{document:{createElement:()=>({}),head:{appendChild:el=>tags[el.id]=el}}});
 const dict=JSON.parse(tags.i18n.textContent);
 const html=payload('src/index_html.h').replace(/%[A-Z_0-9]+%/g,'').replace(/<script\b[^>]*>[\s\S]*?<\/script\s*>/gi,'').replace(/<link\b[^>]*>/gi,'').replace('</title>','</title><style>'+css+'</style>');
 const browser=await chromium.launch({headless:true});const page=await browser.newPage();await page.setContent(html);
 await page.evaluate(()=>{document.querySelectorAll('.page').forEach(e=>e.classList.remove('active'));document.getElementById('shelly').classList.add('active');});
 assert.equal(await page.locator('#shelly .shelly-device-row').count(),7);
 const structure=await page.evaluate(()=>{
  const form=document.getElementById('shellySettingsForm');
  const controls=[...document.querySelectorAll('#shelly input,#shelly select,#shelly button')];
  return {form:controls.every(c=>c.form===form),labels:controls.filter(c=>c.matches('input,select')).every(c=>c.labels.length>0),unique:[...document.querySelectorAll('#shelly [id]')].every(el=>document.querySelectorAll('#'+CSS.escape(el.id)).length===1),
   readOnly:document.getElementById('shellyLightOffTime').readOnly&&document.getElementById('floodAlarmUrl').readOnly&&document.getElementById('floodClearUrl').readOnly,
   password:document.getElementById('shellyPassword').type,
   save:document.getElementById('saveshellysettingsBtn').getAttribute('onclick'),refresh:document.querySelector('#shelly .shelly-refresh-button').getAttribute('onclick')};
 });
 assert.ok(structure.form&&structure.labels&&structure.unique&&structure.readOnly,JSON.stringify(structure));assert.equal(structure.password,'password');assert.match(structure.save,/saveShellySettingsAll/);assert.match(structure.refresh,/refreshShellySchedule/);
 assert.equal(await page.locator('#shelly details').count(),2);
 assert.ok(await page.evaluate(()=>[...document.querySelectorAll('#shelly details')].every(d=>!d.open)),'Advanced settings start collapsed');
 assert.ok(await page.evaluate(()=>!document.getElementById('shellyLightOnTime').closest('.shelly-devices-panel')),'Light schedule is separate from device rows');
 fs.mkdirSync('layout-previews',{recursive:true});
 for(const lang of ['de','en'])for(const width of [1640,1000,800,390]){
  await page.setViewportSize({width,height:1000});await page.evaluate(({dict,lang})=>{document.documentElement.lang=lang;document.querySelectorAll('#shelly [data-i18n]').forEach(el=>{const text=dict[el.dataset.i18n]?.[lang];if(text!==undefined){if(el.dataset.i18nAttr)el.setAttribute(el.dataset.i18nAttr,text);else el.textContent=text;}});},{dict,lang});
  await page.waitForTimeout(350);
  assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1),'Page overflow '+lang+' '+width);
  assert.ok(await page.evaluate(()=>[...document.querySelectorAll('#shelly .runsettings-panel')].every(el=>el.scrollWidth<=el.clientWidth+1)),'Panel overflow '+lang+' '+width);
  const boxes=await page.locator('.shelly-device-row').evaluateAll(els=>els.map(el=>{const r=el.getBoundingClientRect();const ip=el.querySelector('input').getBoundingClientRect();return {x:r.x,y:r.y,height:r.height,bottom:r.bottom,ipX:ip.x,ipWidth:ip.width};}));
  assert.ok(boxes.every(b=>Math.abs(b.x-boxes[0].x)<2),'Rows share one left edge');
  if(width===1640){
    assert.ok(boxes.every(b=>Math.abs(b.ipX-boxes[0].ipX)<2&&Math.abs(b.ipWidth-boxes[0].ipWidth)<2),'Aligned desktop IP column');
    assert.ok(boxes.every(b=>b.height<110),'Compact rows');
    assert.ok(boxes.slice(1).every((b,i)=>b.y-boxes[i].bottom<2),'No gaps between device rows');
  }
  for(const summary of await page.locator('#shelly details > summary').all())await summary.click();
  assert.ok(await page.evaluate(()=>[...document.querySelectorAll('#shelly details')].every(d=>d.open)),'Native details open');
  assert.ok(await page.evaluate(()=>[...document.querySelectorAll('#shelly details input')].every(input=>!input.disabled)),'Collapsed settings remain editable');
  assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1),'Expanded sections do not overflow');
  await page.screenshot({path:`layout-previews/shelly-expanded-${lang}-${width}.png`,fullPage:true});
  for(const summary of await page.locator('#shelly details > summary').all())await summary.click();
  await page.screenshot({path:`layout-previews/shelly-${lang}-${width}.png`,fullPage:true});
  console.log(`Shelly ${lang} ${width}px: aligned rows, native details, valid form, no overflow`);
 }
 await browser.close();
})().catch(e=>{console.error(e);process.exit(1)});
