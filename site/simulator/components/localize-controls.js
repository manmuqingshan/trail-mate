import {translate,subscribeLocale,loadLocale} from '../../i18n/runtime.js';

export async function localizeSimulatorControls(root) {
  await loadLocale('en');
  const update=()=>{
    const text=(selector,key)=>{const node=root.querySelector(selector);if(node)node.textContent=translate(key);};
    text('.explorer-heading .eyebrow','explorer.label');
    text('.explorer-heading h2','explorer.title');
    text('#native-reset','explorer.reset');
    text('.explorer-heading > p','explorer.renderer');
    text('.demo-boundary','explorer.boundary');
    text('.native-caption a','explorer.source');
    const deviceLabel=root.querySelector('.device-preview-select');
    if(deviceLabel?.firstChild?.nodeType===3)deviceLabel.firstChild.textContent=translate('explorer.device')+' ';
    const searchLabel=root.querySelector('.feature-search');
    if(searchLabel?.firstChild?.nodeType===3)searchLabel.firstChild.textContent=translate('explorer.search')+' ';
    root.querySelector('#native-search')?.setAttribute('placeholder',translate('explorer.searchPlaceholder'));
    const protocolLabel=root.querySelector('.native-scenarios label');
    if(protocolLabel?.firstChild?.nodeType===3)protocolLabel.firstChild.textContent=translate('explorer.protocol')+' ';
    text('.native-scenarios > span','explorer.controls');
    const credit=root.querySelector('.map-credit');
    if(credit) {
      const link=document.createElement('a');
      link.href='https://www.openstreetmap.org/copyright';
      link.textContent=translate('explorer.mapCredit');
      credit.replaceChildren(link,document.createTextNode('. '+translate('explorer.mapDetail')));
    }
    root.querySelector('#native-text')?.setAttribute('aria-label',translate('explorer.displayText'));
    root.querySelector('#native-canvas')?.setAttribute('aria-label',translate('explorer.displayText'));
    root.querySelector('#native-controls')?.setAttribute('aria-label',translate('explorer.controls'));
    root.querySelector('#native-navigation')?.setAttribute('aria-label',translate('explorer.search'));
    root.querySelector('.svg-wheel')?.setAttribute('aria-label',translate(root.querySelector('.svg-wheel')?.dataset.control==='trackball'?'explorer.trackball':'explorer.rotary'));
  };
  update();
  return subscribeLocale(update);
}
