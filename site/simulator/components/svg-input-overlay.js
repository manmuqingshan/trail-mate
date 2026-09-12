// Clone only geometry into a separate SVG input layer. Decorative artwork stays inert.
export function createSvgInputOverlay(artwork, device, onKey) {
  const namespace='http://www.w3.org/2000/svg';
  const overlay=document.createElementNS(namespace,'svg');
  overlay.setAttribute('viewBox',artwork.getAttribute('viewBox'));
  overlay.classList.add('device-input-overlay');
  const group=document.createElementNS(namespace,'g');
  // Pager paths use source coordinates; its artwork applies this same translation.
  if(device.id==='pager')group.setAttribute('transform',`translate(${-device.geometry.sourceOffset.x},${-device.geometry.sourceOffset.y})`);
  overlay.append(group);
  const keymap={...device.keymap,...device.pathKeys};
  for(const [index,box] of (device.rectangleKeys||[]).entries()) {
    const id=`input-rect-${index}`;
    const rectangle=document.createElementNS(namespace,'rect');
    rectangle.id=id;
    for(const attribute of ['x','y','width','height'])rectangle.setAttribute(attribute,box[attribute]);
    artwork.append(rectangle);
    keymap[id]=box.key;
  }
  for(const [id,key] of Object.entries(keymap)) {
    const source=artwork.querySelector(`[id="${id}"]`);
    if(!source)throw new Error(`Missing ${device.id} input geometry ${id}`);
    const hit=document.createElementNS(namespace,source.localName);
    for(const name of ['d','cx','cy','rx','ry','x','y','width','height','transform']) {
      if(source.hasAttribute(name))hit.setAttribute(name,source.getAttribute(name));
    }
    hit.setAttribute('fill','transparent');
    hit.setAttribute('role','button');
    hit.setAttribute('tabindex','0');
    hit.setAttribute('aria-label',key);
    hit.style.pointerEvents='all';
    hit.addEventListener('pointerdown',event=>{event.preventDefault();hit.setPointerCapture(event.pointerId);hit.classList.add('pressed');onKey(key,true);});
    for(const event of ['pointerup','pointercancel','lostpointercapture'])hit.addEventListener(event,()=>{
      if(hit.classList.contains('pressed')){hit.classList.remove('pressed');onKey(key,false);}
    });
    hit.addEventListener('keydown',event=>{if(!event.repeat&&(event.key==='Enter'||event.key===' ')){event.preventDefault();onKey(key,true);}});
    hit.addEventListener('keyup',event=>{if(event.key==='Enter'||event.key===' '){event.preventDefault();onKey(key,false);}});
    group.append(hit);
  }
  return overlay;
}
