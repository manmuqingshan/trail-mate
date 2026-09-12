import {pagerKeymap,pagerPathKeys} from './pager-keymap.js';
export default {
  keymap:pagerKeymap,
  pathKeys:pagerPathKeys,
  id:'pager', name:'T-LoRa Pager', width:480, height:222,
  manufacturer:'lilygo',
  touch:false, unsupportedFeatures:[],
  controls:'Encoder + keyboard',
  note:'SX1262 and LR1121 require different firmware. Shared screen layout does not imply identical radio capabilities.',
  artwork:new URL('../../assets/device/lilygo/lilygo-tlora-pager.svg',import.meta.url).href,
  geometry:{
    viewBox:{width:587.9104,height:687.70074},
    sourceOffset:{x:245.94,y:153.75},
    screen:{x:99.98001,y:274.5,width:340.13,height:155.95},
    rotary:{x:501.18,y:285.42999,width:18.82,height:89.07},
  },
};
