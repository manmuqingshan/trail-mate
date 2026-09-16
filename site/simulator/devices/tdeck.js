import {tdeckKeys} from './tdeck-keymap.js';
export default {
  rectangleKeys:tdeckKeys,
  id:'tdeck', name:'T-Deck', width:320, height:240,
  manufacturer:'lilygo',
  touch:true, unsupportedFeatures:['sstv','walkie'],
  controls:'Touch + trackball + keyboard',
  note:'320 × 240 compact layout. GNSS, audio and other peripherals depend on the hardware and selected build.',
  artwork:new URL('../../assets/device/lilygo/t-deck.svg',import.meta.url).href,
  geometry:{
    viewBox:{width:622.29,height:968.69},
    sourceOffset:{x:696.3,y:500.37},
    screen:{x:59.35,y:133.27,width:500.52,height:377.46},
    trackball:{x:291.87,y:551.62,width:52.8,height:52.8},
  },
};
