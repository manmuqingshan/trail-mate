import assert from 'node:assert/strict';
import {access} from 'node:fs/promises';
import {devices} from '../../site/simulator/devices/registry.js';
import {fitDeviceSize} from '../../site/simulator/components/device-size.js';
import {containScreen} from '../../site/simulator/components/screen-box.js';
for(const device of Object.values(devices)) {
  await access(new URL(device.artwork));
  const {viewBox,screen}=device.geometry;
  const fitted=containScreen(screen,device.width,device.height);
  assert.ok(Math.abs(fitted.width/fitted.height-device.width/device.height)<1e-9);
  assert.ok(fitted.x>=screen.x&&fitted.y>=screen.y);
  assert.ok(fitted.x+fitted.width<=screen.x+screen.width+1e-8);
  assert.ok(fitted.y+fitted.height<=screen.y+screen.height+1e-8);
  for(const box of [screen,device.geometry.rotary,device.geometry.trackball].filter(Boolean)) {
    assert.ok(box.x>=0&&box.y>=0);
    assert.ok(box.x+box.width<=viewBox.width&&box.y+box.height<=viewBox.height);
  }
  for(const viewport of [280,375,640,1440,2560]) {
    const size=fitDeviceSize(viewBox,viewport);
    assert.ok(size.width<=viewport-24);
    assert.ok(size.width<=480&&size.height<=560.001);
    assert.ok(Math.abs(size.width/size.height-viewBox.width/viewBox.height)<1e-9);
  }
  console.log(`${device.id}: artwork exists; screen/input bounds and five fit widths passed`);
}
