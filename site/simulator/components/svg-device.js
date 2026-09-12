import {createSvgInputOverlay} from './svg-input-overlay.js';
import {fitDeviceSize} from './device-size.js';
import {containScreen} from './screen-box.js';
import {translate} from '../../i18n/runtime.js';

export async function mountSvgDevice(container, display, device, onKey, onEncoder) {
  const response=await fetch(device.artwork);
  if(!response.ok)throw new Error(`Device artwork: ${response.status}`);
  const svg=new DOMParser().parseFromString(await response.text(),'image/svg+xml').documentElement;
  if(svg.localName!=='svg')throw new Error('Invalid device SVG');
  const frame=document.createElement('div');
  frame.className='svg-device';
  const art=document.createElement('img');
  art.src=device.artwork; art.alt=''; art.draggable=false;
  frame.append(art,display,createSvgInputOverlay(svg,device,onKey));
  const {viewBox}=device.geometry;
  const screen=containScreen(device.geometry.screen,device.width,device.height);
  const rotary=device.geometry.rotary || device.geometry.trackball;
  display.style.cssText=`position:absolute;left:${screen.x/viewBox.width*100}%;top:${screen.y/viewBox.height*100}%;width:${screen.width/viewBox.width*100}%;height:${screen.height/viewBox.height*100}%;margin:0;border:0;`;
  if(!device.touch)display.inert=true;
  if(rotary) {
    const wheel=document.createElement('button');
    wheel.className='svg-wheel';
    wheel.dataset.control=device.geometry.trackball?'trackball':'rotary';
    wheel.dataset.i18nAria=`explorer.${wheel.dataset.control}`;
    wheel.setAttribute('aria-label',translate(wheel.dataset.i18nAria));
    wheel.style.cssText=`left:${rotary.x/viewBox.width*100}%;top:${rotary.y/viewBox.height*100}%;width:${rotary.width/viewBox.width*100}%;height:${rotary.height/viewBox.height*100}%;`;
    let y=null,dragged=false;
    wheel.onpointerdown=e=>{y=e.clientY;dragged=false;wheel.setPointerCapture(e.pointerId);};
    wheel.onpointermove=e=>{if(y===null)return;const steps=Math.trunc((e.clientY-y)/8);if(steps){dragged=true;y+=steps*8;onEncoder(steps);}};
    wheel.onpointerup=()=>{y=null;};
    wheel.onpointercancel=()=>{y=null;dragged=true;};
    wheel.onclick=()=>{if(!dragged){onKey('ENTER',true);onKey('ENTER',false);}};
    wheel.addEventListener('wheel',e=>{e.preventDefault();if(e.deltaY)onEncoder(Math.sign(e.deltaY));},{passive:false});
    frame.append(wheel);
  }
  container.replaceChildren(frame);
  container.classList.add('svg-device-viewport');
  return ()=>{
    // Bound the whole device, independently of framebuffer resolution.
    const {width,height}=fitDeviceSize(viewBox,container.clientWidth);
    frame.style.width=`${width}px`;
    frame.style.height=`${height}px`;
  };
}
