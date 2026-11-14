import fs from 'node:fs';import vm from 'node:vm';import assert from 'node:assert/strict';
const src=fs.readFileSync(new URL('../src/java_script.h',import.meta.url),'utf8');
function extract(signature){let a=src.indexOf(signature),i=src.indexOf('{',a)+1,depth=1;while(depth){depth+=(src[i]==='{')-(src[i]==='}');i++;}return src.slice(a,i);}
for(const lang of ['de','en']){
 const classes=new Set(),state={style:{},innerHTML:'',classList:{remove(...xs){xs.forEach(x=>classes.delete(x));},add(x){classes.add(x);}}};
 const ctx={currentLang:lang,window:{},document:{getElementById:id=>id==='state'?state:null},shellyMap:{main:['state','info','settings.shelly.main']},isNum:v=>typeof v==='number'&&Number.isFinite(v)};
 vm.createContext(ctx);vm.runInContext(extract('function setShellyStateClass(')+'\n'+extract('function fmtNum(')+'\n'+extract('function setSwitchWithMetrics('),ctx);
 for(const gen of [1,2,3])for(const status of [null,undefined,true,false]){
  ctx.status=status;ctx.gen=gen;vm.runInContext("setSwitchWithMetrics('main',status,null,null,null,gen)",ctx);
  assert.equal(classes.has('shelly-on'),status===true);assert.equal(classes.has('shelly-off'),status===false);assert.equal(classes.has('shelly-unknown'),status!==true&&status!==false);
  if(status==null)assert.equal(state.innerHTML,lang==='de'?'Unbekannt':'Unknown');
 }
}
console.log('Shelly UI: unknown is neutral, never OFF; Gen1/Gen2/Gen3 and DE/EN passed');
