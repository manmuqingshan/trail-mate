import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';

for(const device of ['pager','tdeck','wio-tracker-l2']) {
  const base=new URL(`../../site/assets/native/${device}`,import.meta.url);
  const {default:create}=await import(`${base}.js`);
  const bytes=await readFile(new URL(`${base}.data`));
  const runtime=await create({wasmBinary:await readFile(new URL(`${base}.wasm`)),getPreloadedPackage(){return bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength);},print(){},printErr(){}});
  runtime._native_init(device==='pager'?0:1);
  const tick=()=>{for(let i=0;i<8;i++)runtime._native_tick(50);};
  const snapshot=()=>JSON.parse(runtime.UTF8ToString(runtime._native_snapshot()));
  const click=text=>{const button=snapshot().find(item=>item.button&&item.text===text);assert.ok(button,`${device}: missing ${text}`);runtime._native_activate(button.id);tick();};
  runtime._native_open_page(14);tick();
  assert.ok(snapshot().some(item=>item.text==='No tracks yet'&&!item.button));
  assert.ok(!snapshot().some(item=>item.text==='Back'));
  const action=snapshot().find(item=>item.button&&item.text==='Start recording');
  assert.ok(action);
  assert.ok(action.x+action.w>=(device==='pager'?480:320)-8,'Primary action aligns to content right edge');
  click('Start recording');
  assert.ok(snapshot().some(item=>item.text==='Recording'));
  const stop=snapshot().find(item=>item.button&&item.text==='Stop recording');
  assert.equal(stop.w,action.w,'Start and stop share a stable button width');
  assert.equal(stop.x,action.x,'Start and stop share a stable right-aligned position');
  assert.ok(snapshot().some(item=>item.button&&item.text.endsWith('.gpx')),'New recording appears without leaving the page');
  click('Routes');
  assert.ok(!snapshot().some(item=>['Recording','Stop recording'].includes(item.text)),'Route mode has its own status and actions');
  click('Tracks');
  assert.ok(snapshot().some(item=>item.text==='Recording'),'Changing tabs does not stop recording');
  click('Stop recording');
  assert.ok(snapshot().some(item=>item.text==='Not recording'));
  click('Routes');
  assert.ok(!snapshot().some(item=>['Start recording','Stop recording','Back'].includes(item.text)));
  assert.ok(snapshot().some(item=>item.text.includes('sample-route')));
  click('Tracks');
  assert.ok(snapshot().some(item=>item.button&&item.text==='Start recording'));
  click('\uf053');
  assert.equal(runtime._native_page_value(),10,'Top-bar back remains available without a list Back row');
  console.log(`${device}: empty state, stable action width, recording, list refresh, route mode and top-bar back passed.`);
}
