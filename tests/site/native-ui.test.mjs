import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

for (const [device, width, height] of [['pager',480,222],['tdeck',320,240],['wio-tracker-l2',320,240]]) {
  const base=new URL(`../../site/assets/native/${device}`,import.meta.url);
  const {default:create}=await import(`${base}.js`);
  let frameCount=0;
  const packageBytes=await readFile(new URL(`${base}.data`));
  const packageData=packageBytes.buffer.slice(packageBytes.byteOffset,packageBytes.byteOffset+packageBytes.byteLength);
  const runtime=await create({wasmBinary:await readFile(new URL(`${base}.wasm`)),getPreloadedPackage(){return packageData;},onFrame(_ptr,w,h){assert.equal(w,width);assert.equal(h,height);frameCount++;},print(){},printErr(message){console.error(message);}});
  runtime._native_init(device==='pager'?0:1);
  const snapshot=()=>JSON.parse(runtime.UTF8ToString(runtime._native_snapshot()));
  const click=text=>{const button=snapshot().find(x=>x.button&&x.text===text);assert.ok(button,`${device}: missing native button ${text}`);runtime._native_activate(button.id);runtime._native_tick(50);};
  for(const label of ['1','+','2','='])click(label);
  assert.ok(snapshot().some(x=>!x.button&&x.text==='3'),`${device}: native 1+2 must render 3`);
  click('2ND');
  assert.ok(snapshot().some(x=>x.button&&x.text==='sin-1'),`${device}: second function layer`);
  click('MODE');
  assert.ok(snapshot().some(x=>x.text==='RAD'),`${device}: native angle mode`);
  const pages=[];
  for(const page of [1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,0]) {
    runtime._native_open_page(page);
    for(let i=0;i<3;i++)runtime._native_tick(50);
    const labels=snapshot();
    assert.ok(labels.length>0,`${device}: page ${page} must render actual labels`);
    pages.push({page,labels:labels.length});
  }
  assert.ok(frameCount>10);
  runtime._native_open_page(7);
  const editor=snapshot().find(item=>item.input);
  assert.ok(editor,`${device}: compose editor exists`);
  runtime._native_focus(editor.id);
  runtime.ccall('native_set_text',null,['number','string'],[editor.id,'']);
  for(const key of ['A',' ','B']) {
    runtime._native_key_state(key.charCodeAt(0),1);
    runtime._native_key_state(key.charCodeAt(0),0);
  }
  runtime._native_tick(50);
  assert.equal(snapshot().find(item=>item.input)?.text,'A B',`${device}: keyboard space reaches compose`);
  for(const expectedPage of [6,5,10]) {
    // The actual top-bar button uses LV_SYMBOL_LEFT; "Back" is a list label.
    if(expectedPage===10)click('\uf053');
    else runtime._native_key(27);
    for(let i=0;i<5;i++)runtime._native_tick(50);
    assert.equal(runtime._native_page_value(),expectedPage,`${device}: chat back must preserve navigation depth`);
    assert.ok(snapshot().length>0,`${device}: chat return must not blank the display`);
  }
  runtime._native_open_page(10);
  runtime._native_key(10);
  for(let i=0;i<5;i++)runtime._native_tick(50);
  assert.notEqual(runtime._native_page_value(),10,`${device}: launcher confirm must enter the focused app`);
  // Returning must work inside the native host, without a webpage onExit callback.
  runtime._native_open_page(10);
  runtime._native_open_page(0);
  runtime._native_key(27);
  for(let i=0;i<5;i++)runtime._native_tick(50);
  assert.ok(snapshot().some(x=>x.text==='Map'),`${device}: native calculator back must restore launcher`);
  assert.equal(runtime._native_page_value(),10,`${device}: exit must update native page ownership`);
  for(let repeat=0;repeat<3;repeat++) {
    runtime._native_open_page(0);
    runtime._native_key(27);
    for(let i=0;i<5;i++)runtime._native_tick(50);
    assert.equal(runtime._native_page_value(),10);
    assert.ok(snapshot().some(x=>x.text==='Map'),`${device}: repeated return must not blank the launcher`);
  }
  console.log(`${device}: native arithmetic, function layer, DEG/RAD and ${pages.length} page lifecycles passed; ${frameCount} frames.`);
}
