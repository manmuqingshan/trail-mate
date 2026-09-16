import {devices} from './simulator/devices/registry.js';
import {mountSvgDevice} from './simulator/components/svg-device.js';
import {localizeSimulatorControls} from './simulator/components/localize-controls.js';
import {translate,subscribeLocale,loadLocale,localizeFeature} from './i18n/runtime.js';
await loadLocale('en');
let resizeSvgDevice;
const root = document.querySelector('#explorer');
const nativeAssetRoot=new URL('./assets/native/',import.meta.url);
let buildManifest, guideModule;
try {
  const response=await fetch(new URL('manifest.json',nativeAssetRoot),{cache:'no-store'});
  if(!response.ok)throw new Error(`Native manifest: HTTP ${response.status}`);
  buildManifest=await response.json();
  const guideUrl=new URL('./explorer-data.js',import.meta.url);guideUrl.searchParams.set('v',buildManifest.guide);
  guideModule=await import(guideUrl.href);
} catch(error){root.innerHTML='<p data-i18n="explorer.startError"></p>';root.firstElementChild.textContent=translate('explorer.startError');throw error;}
const {FEATURES}=guideModule;
const params = new URLSearchParams(location.search);
const device = Object.hasOwn(devices,params.get('demo-device')) ? params.get('demo-device') : 'pager';
const profile = devices[device];
const pages = {calculator:0,gps:1,usb:2,walkie:3,sstv:4,chat:5,extensions:8,network:9,home:10,settings:11,mqtt:11,contacts:12,team:13,tracker:14,route:14,map:15,probe:16,help:17,power:18};
const unsupportedByDevice = new Set(profile.unsupportedFeatures);
let currentPage = Object.hasOwn(pages,params.get('feature')) && !unsupportedByDevice.has(params.get('feature')) ? params.get('feature') : 'home';
let runtime, frameData;
let lastSnapshot = '', lastTime = performance.now(), poweredOff = false;
let observedNativePage=-1;
const escape = value => String(value).replace(/[&<>"']/g, c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'})[c]);

root.innerHTML = `<div class="explorer-heading"><div><p class="eyebrow">FIRMWARE UI / 固件原生界面</p><h2>The actual interface. In your browser.</h2></div><p>Original C++ pages · LVGL 9.4<br>Firmware fonts, icons, layouts and controls.</p></div>
<div class="explorer-toolbar"><label class="device-preview-select">Preview device / 预览设备 <select id="native-device">${Object.values(devices).map(item=>`<option value="${item.id}" ${device===item.id?'selected':''}>${item.name}</option>`).join('')}</select></label><span class="demo-badge">LVGL · WEBASSEMBLY</span><button id="native-reset">Reset demo</button></div>
<div class="native-layout"><nav id="native-navigation" aria-label="Firmware pages"><label class="feature-search">Find a feature / 查找<input id="native-search" type="search" placeholder="Maps, chat, 计算器…"></label><div id="native-page-list"></div></nav>
<div class="native-stage"><div class="native-caption"><span>${profile.name} · ${profile.width} × ${profile.height}</span><a href="https://github.com/vicliu624/trail-mate/tree/main/modules/ui_shared/src/ui/screens">Firmware source ↗</a></div><p id="native-status" role="status">Loading the native firmware renderer…</p><div class="native-scroll"><div class="hardware-shell" data-shell="${device}"><div class="shell-top" aria-hidden="true"></div><div class="shell-speaker" aria-hidden="true"></div><div id="native-display" tabindex="0"><canvas id="native-canvas" width="${profile.width}" height="${profile.height}" aria-label="Native LVGL firmware display"></canvas><div id="native-controls" aria-label="Native firmware controls"></div></div>${device==='tdeck'?'<div class="tdeck-trackbar" role="group" aria-label="T-Deck trackball"><button class="trackball" data-input="enter" aria-label="Trackball press">●</button></div>':'<div class="pager-antennas" aria-hidden="true"><i></i><i></i></div><div class="pager-side-controls" role="group" aria-label="Pager rotary and backspace"><button class="pager-rotary" data-input="enter" aria-label="Rotary press"></button><button class="pager-backspace" data-key="⌫" aria-label="Backspace">⌫</button></div>'}<div class="hardware-keyboard" aria-label="Full hardware keyboard"></div></div></div>
<div class="native-scenarios"><label>Sample protocol <select id="native-protocol"><option value="1">Meshtastic</option><option value="2">MeshCore</option><option value="4">Reticulum</option></select></label><span>Use the hardware controls and keyboard shown on the device.</span></div>
<p class="demo-boundary">Rendered by the firmware’s LVGL C++ pages — not screenshots or HTML imitations.<br>Radio, GNSS, battery and storage inputs are simulated. No device is connected.</p><p class="map-credit">Sample map data © <a href="https://www.openstreetmap.org/copyright">OpenStreetMap contributors</a>. One cached OSM region at zoom 12; the route is a synthetic test fixture.</p><pre id="native-text" class="sr-only" aria-label="Firmware display text"></pre></div><aside class="feature-guide" id="native-guide"></aside></div>`;

const canvas = root.querySelector('canvas');
const context = canvas.getContext('2d');
const display = root.querySelector('#native-display');
const controls = root.querySelector('#native-controls');
const status = root.querySelector('#native-status');
let statusKey='explorer.loading',statusParams={};
const setPreviewStatus=(key,params={})=>{statusKey=key;statusParams=params;status.textContent=translate(key,params);};
setPreviewStatus(statusKey);
subscribeLocale(()=>setPreviewStatus(statusKey,statusParams));
const guide = root.querySelector('#native-guide');
const keyboard = root.querySelector('.hardware-keyboard');
const keyboardRows = device === 'pager'
  ? [['Q|1','W|2','E|3','R|4','T|5','Y|6','U|7','I|8','O|9','P|0'],['A|*','S|/','D|+','F|-','G|=','H|:','J|;','K|,','L|@','⌫'],['ALT','Z|-','X|$','C|`','V|?','B|!','N|n','M|m','CAP']]
  : [['Q|#','W|1','E|2','R|3','T|(','Y|)','U|_','I|i','O|+','P|@'],['A|*','S|4','D|5','F|6','G|/','H|:','J|;','K|k','L|9'],['ALT','Z|7','X|8','C|9','V|?','B|!','N|n','M|$','⌫'],['NOOP','SHIFT','MIC|0','SPACE','SYM','SHIFT','NOOP']];
keyboard.innerHTML=keyboardRows.map(row=>`<div class="keyboard-row">${row.map(spec=>{const [key,label]=spec.split('|');return `<button type="button" data-key="${key}">${label?`<small>${label}</small>`:''}<b>${key}</b></button>`}).join('')}</div>`).join('')+(device==='pager'?'<button type="button" class="pager-space" data-key="SPACE" aria-label="Space">SPACE</button>':'');
keyboard.addEventListener('pointerdown',event=>{const key=event.target.closest('[data-key]')?.dataset.key;if(!key||key==='NOOP'||!runtime)return;event.preventDefault();event.target.setPointerCapture?.(event.pointerId);const code=key==='SPACE'?32:key==='ENTER'?10:key==='⌫'?8:key==='TAB'?9:key==='MIC'?48:key==='CAP'||key==='SHIFT'?16:key==='ALT'?18:key==='SYM'?32:key==='FN'?17:key.charCodeAt(0);runtime._native_key_state(code,1);});
keyboard.addEventListener('pointerup',event=>{const key=event.target.closest('[data-key]')?.dataset.key;if(!key||key==='NOOP'||!runtime)return;const code=key==='SPACE'?32:key==='ENTER'?10:key==='⌫'?8:key==='TAB'?9:key==='MIC'?48:key==='CAP'||key==='SHIFT'?16:key==='ALT'?18:key==='SYM'?32:key==='FN'?17:key.charCodeAt(0);runtime._native_key_state(code,0);});
const trackball=root.querySelector('.trackball');
const pagerRotary=root.querySelector('.pager-rotary');
const pagerBackspace=root.querySelector('.pager-backspace');
// Pointer capture keeps a touch drag on the physical wheel, even outside its bounds.
let pagerWheelY=null, pagerWheelDragged=false;
pagerRotary?.addEventListener('pointerdown',event=>{
  if(!runtime||poweredOff||event.button!==0)return;
  pagerWheelY=event.clientY; pagerWheelDragged=false;
  pagerRotary.setPointerCapture(event.pointerId);
  pagerRotary.classList.add('is-pressed');
});
pagerRotary?.addEventListener('pointermove',event=>{
  if(pagerWheelY===null||!runtime||poweredOff)return;
  const steps=Math.trunc((event.clientY-pagerWheelY)/10);
  if(!steps)return;
  pagerWheelDragged=true; pagerWheelY+=steps*10;
  for(let i=0;i<Math.abs(steps);i++)runtime._native_encoder(Math.sign(steps));
  pagerRotary.style.backgroundPositionY=`${pagerWheelY}px`;
  updateControls();
});
for(const type of ['pointerup','pointercancel','lostpointercapture']){
  pagerRotary?.addEventListener(type,()=>{pagerWheelY=null;pagerRotary.classList.remove('is-pressed');});
}
// A completed drag must not also confirm the newly selected menu item.
pagerRotary?.addEventListener('click',event=>{
  if(pagerWheelDragged){event.preventDefault();event.stopImmediatePropagation();pagerWheelDragged=false;}
},true);
pagerBackspace?.addEventListener('pointerdown',event=>{
  if(event.button!==0)return;
  pagerBackspace.setPointerCapture(event.pointerId);
  pagerBackspace.classList.add('is-pressed');
});
for(const type of ['pointerup','pointercancel','lostpointercapture']){
  pagerBackspace?.addEventListener(type,()=>pagerBackspace.classList.remove('is-pressed'));
}
pagerBackspace?.addEventListener('click',()=>{if(runtime&&!poweredOff){runtime._native_key(8);updateControls();}});
pagerRotary?.addEventListener('wheel',event=>{if(runtime&&!poweredOff){event.preventDefault();runtime._native_encoder(event.deltaY>0?1:-1);updateControls();}},{passive:false});
let trackballPoint;
trackball?.addEventListener('wheel',event=>{if(!runtime||poweredOff)return;event.preventDefault();runtime._native_encoder(event.deltaY>0?1:-1);updateControls();},{passive:false});
trackball?.addEventListener('pointerdown',event=>{trackballPoint=[event.clientX,event.clientY];trackball.setPointerCapture?.(event.pointerId);});
trackball?.addEventListener('pointermove',event=>{if(!trackballPoint||!runtime||poweredOff)return;const dx=event.clientX-trackballPoint[0],dy=event.clientY-trackballPoint[1];if(Math.abs(dx)+Math.abs(dy)<8)return;runtime._native_encoder(Math.abs(dx)>Math.abs(dy)?(dx>0?1:-1):(dy>0?1:-1));trackballPoint=[event.clientX,event.clientY];updateControls();});
trackball?.addEventListener('pointerup',()=>{trackballPoint=null;});

function renderNavigation(query='') {
  root.querySelector('#native-page-list').innerHTML = FEATURES
    .filter(feature=>!unsupportedByDevice.has(feature.id))
    .map(localizeFeature)
    .filter(feature=>`${feature.name} ${feature.summary}`.toLocaleLowerCase(document.documentElement.lang).includes(query.toLocaleLowerCase(document.documentElement.lang)))
    .map(feature=>`<button data-page="${feature.id}" aria-current="${feature.id===currentPage?'page':'false'}">${escape(feature.name)}</button>`)
    .join('') || `<p>${escape(translate('explorer.noMatches'))}</p>`;
}

function renderGuide(id) {
  const base=FEATURES.find(f=>f.id===id) || FEATURES[0];
  const item=localizeFeature(base);
  guide.innerHTML=`<p class="eyebrow">${escape(item.name)}</p><h3>${escape(item.summary)}</h3><p>${escape(item.detail)}</p><div class="try-this"><strong>${escape(translate('guide.try'))}</strong><p>${escape(translate('guide.try.'+(['calculator','network','route','mqtt','probe'].includes(id)?id:'default')))}</p></div><h4>${escape(translate('guide.requirements'))}</h4><p>${escape(item.needs)}</p><a class="guide-doc" href="https://github.com/vicliu624/trail-mate/tree/main/${item.source}">${escape(translate('guide.reference'))}</a><div class="device-note"><strong>${escape(translate('device.'+device+'.controls'))}</strong><p>${escape(translate('device.'+device+'.note'))}</p></div>`;
}

function fitDisplay() {
  const choice='auto';
  if(resizeSvgDevice){resizeSvgDevice(choice);return;}
  const scale=choice==='auto'?Math.min(2,root.querySelector('.native-scroll').clientWidth/profile.width):Number(choice);
  const shell=root.querySelector('.hardware-shell');
  if(shell&&device==='pager') shell.style.width=choice==='1'?'736px':'';
  display.style.width=`${profile.width*scale}px`;
  display.style.height=`${profile.height*scale}px`;
}

function updateControls() {
  if(!runtime||poweredOff)return;
  const json=runtime.UTF8ToString(runtime._native_snapshot());
  if(json===lastSnapshot)return;
  lastSnapshot=json;
  const labels=JSON.parse(json);
  const active=document.activeElement;
  const focusId=active?.dataset.nativeId;
  const selection=active?.tagName==='TEXTAREA'?[active.selectionStart,active.selectionEnd]:null;
  controls.replaceChildren();
  const uniqueControls=new Map();
  for(const item of labels.filter(x=>x.button||x.input))if(!uniqueControls.has(item.id))uniqueControls.set(item.id,item);
  for(const item of uniqueControls.values()) {
    const control=document.createElement(item.input?'textarea':'button');
    control.dataset.nativeId=String(item.id);
    control.dataset.nativeText=item.input?'Text entry':item.text;
    control.setAttribute('aria-label',item.input?'Device text entry':item.text);
    control.style.cssText=`left:${item.x/profile.width*100}%;top:${item.y/profile.height*100}%;width:${item.w/profile.width*100}%;height:${item.h/profile.height*100}%;`;
    if(item.input) {
      control.value=item.text;
      control.spellcheck=false;
      control.addEventListener('input',()=>{runtime.ccall('native_set_text',null,['number','string'],[item.id,control.value]);});
    } else {
      control.type='button';
      control.addEventListener('click',event=>{if(event.detail===0){runtime._native_activate(item.id);updateControls();}});
    }
    control.addEventListener('focus',()=>runtime._native_focus(item.id));
    controls.append(control);
    if(focusId===String(item.id)) {
      control.focus({preventScroll:true});
      if(selection&&item.input)control.setSelectionRange(...selection);
    }
  }
  root.querySelector('#native-text').textContent=labels.filter(x=>!x.input).map(x=>x.text).join('\n');
}

function openPage(id) {
  if(!FEATURES.some(f=>f.id===id) || unsupportedByDevice.has(id))return;
  currentPage=id;
  if(runtime&&!poweredOff) {
    try {runtime._native_open_page(pages[id]);observedNativePage=pages[id];lastSnapshot='';updateControls();setPreviewStatus('explorer.running');}
    catch(error){setPreviewStatus('explorer.pageError');console.error(error);}
  }
  renderGuide(id);
  renderNavigation(root.querySelector('#native-search').value);
  const url=new URL(location.href);url.searchParams.set('feature',id);url.searchParams.set('demo-device',device);history.replaceState(null,'',url);
  fitDisplay();
}

root.querySelector('#native-navigation').addEventListener('click',event=>{const button=event.target.closest('[data-page]');if(button)openPage(button.dataset.page);});
root.querySelector('#native-search').addEventListener('input',event=>renderNavigation(event.target.value));
root.querySelector('#native-device').addEventListener('change',event=>{const url=new URL(location.href);url.searchParams.set('demo-device',event.target.value);url.searchParams.set('device',event.target.value==='tdeck'?'tdeck':'tlora-pager-sx1262');location.href=url;});
root.querySelector('#native-reset').addEventListener('click',()=>location.reload());
root.querySelector('#native-protocol').addEventListener('change',event=>{if(runtime){runtime._native_protocol(Number(event.target.value));openPage(currentPage);}});
root.querySelectorAll('[data-input]').forEach(button=>button.addEventListener('click',event=>{const action=event.currentTarget.dataset.input;if(!runtime||!action||poweredOff)return;if(action==='home')openPage('home');else if(action==='enter')runtime._native_key(10);else if(action==='back')runtime._native_key(27);else if(action==='backlight')runtime._native_key(75);else if(action==='track-left')runtime._native_encoder(-1);else if(action==='track-right')runtime._native_encoder(1);else if(action==='touch')display.focus({preventScroll:true});else runtime._native_encoder(action==='previous'?-1:1);updateControls();}));

function pointer(event,down) {
  if(device==='pager'||!runtime||poweredOff||event.target.tagName==='TEXTAREA')return;
  const bounds=canvas.getBoundingClientRect();
  runtime._native_pointer(Math.round((event.clientX-bounds.left)*canvas.width/bounds.width),Math.round((event.clientY-bounds.top)*canvas.height/bounds.height),down?1:0);
}
display.addEventListener('pointerdown',event=>{if(event.target.tagName==='TEXTAREA')return;display.focus({preventScroll:true});display.setPointerCapture(event.pointerId);pointer(event,true);});
display.addEventListener('pointermove',event=>{if(event.buttons)pointer(event,true);});
display.addEventListener('pointerup',event=>{pointer(event,false);updateControls();});
display.addEventListener('pointercancel',()=>runtime?._native_pointer(0,0,0));
const keyCodes={Enter:10,Escape:27,Backspace:8,Delete:127,ArrowUp:17,ArrowDown:18,ArrowRight:19,ArrowLeft:20};
display.addEventListener('keydown',event=>{
  if(!runtime||poweredOff||event.target.tagName==='TEXTAREA'||event.key==='Tab'||event.ctrlKey||event.metaKey||event.altKey)return;
  const key=keyCodes[event.key]??(event.key.length===1?event.key.charCodeAt(0):null);
  if(key!==null){event.preventDefault();runtime._native_key_state(key,1);updateControls();}
});
window.addEventListener('keyup',event=>{if(event.code==='Space')runtime?._native_key_state(32,0);});
window.addEventListener('blur',()=>{runtime?._native_pointer(0,0,0);runtime?._native_key_state(32,0);});
new ResizeObserver(fitDisplay).observe(root.querySelector('.native-scroll'));
renderNavigation();renderGuide(currentPage);fitDisplay();
await localizeSimulatorControls(root);
subscribeLocale(()=>renderGuide(currentPage));
{
  resizeSvgDevice=await mountSvgDevice(root.querySelector('.native-scroll'),display,profile,
    (key,pressed)=>{if(!runtime||poweredOff)return;const code={ENTER:10,BACK:27,BACKSPACE:8,SPACE:32,CAP:16,SHIFT:16,ALT:18,SYM:32,MIC:48}[key]??key.charCodeAt(0);runtime._native_key_state(code,pressed?1:0);updateControls();},
    steps=>{if(!runtime||poweredOff)return;for(let i=0;i<Math.abs(steps);i++)runtime._native_encoder(Math.sign(steps));updateControls();});
  fitDisplay();
}
new MutationObserver(()=>renderNavigation(root.querySelector('#native-search').value)).observe(document.documentElement,{attributes:true,attributeFilter:['lang']});

try {
  const version=buildManifest.devices[device].version;
  const assetUrl=name=>{const url=new URL(name,nativeAssetRoot);url.searchParams.set('v',version);return url.href;};
  const [{default:create},packageResponse]=await Promise.all([import(assetUrl(`${device}.js`)),fetch(assetUrl(`${device}.data`))]);
  if(!packageResponse.ok)throw new Error(`Native data package: HTTP ${packageResponse.status}`);
  const packageData=await packageResponse.arrayBuffer();
  runtime=await create({
    locateFile(name){return assetUrl(name);},
    getPreloadedPackage(){return packageData;},
    onFrame(ptr,width,height) {
      if(!runtime||poweredOff)return;
      if(canvas.width!==width||canvas.height!==height){canvas.width=width;canvas.height=height;frameData=null;}
      frameData??=context.createImageData(width,height);
      const pixels=new Uint16Array(runtime.HEAPU8.buffer,ptr,width*height);
      for(let i=0;i<pixels.length;i++){const p=pixels[i],j=i*4;frameData.data[j]=((p>>11)&31)*255/31;frameData.data[j+1]=((p>>5)&63)*255/63;frameData.data[j+2]=(p&31)*255/31;frameData.data[j+3]=255;}
      context.putImageData(frameData,0,0);
    },
    onExit(){queueMicrotask(()=>openPage('home'));},
    onPowerOff(){poweredOff=true;canvas.style.filter='brightness(0)';controls.replaceChildren();setPreviewStatus('explorer.off');},
    printErr(message){console.error(message);}
  });
  runtime._native_init(device==='pager'?0:1);
  openPage(currentPage);
  function frame(time){
    if(!poweredOff){
      runtime._native_tick(Math.min(100,Math.max(1,Math.round(time-lastTime))));updateControls();
      const choice=root.querySelector('#native-protocol');if(document.activeElement!==choice)choice.value=String(runtime._native_protocol_value());
      const nativePage=runtime._native_page_value();
      if(currentPage!=='pc'&&nativePage!==observedNativePage){
        observedNativePage=nativePage;
        const next=nativePage===6||nativePage===7?'chat':Object.keys(pages).find(id=>pages[id]===nativePage);
        if(next&&next!==currentPage){currentPage=next;renderGuide(next);renderNavigation(root.querySelector('#native-search').value);const url=new URL(location.href);url.searchParams.set('feature',next);history.replaceState(null,'',url);}
      }
    }
    lastTime=time;requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
} catch(error){setPreviewStatus('explorer.startError');console.error(error);}
