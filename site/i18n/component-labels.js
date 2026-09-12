// Kept external to the React bundle so this imports the page's runtime instance.
import {translate,subscribeLocale,loadLocale} from './runtime.js';
export async function watchComponentLabels(listener) {
  await loadLocale('en');
  const update=()=>listener({
    manufacturer:translate('common.manufacturer'),
    device:translate('common.device'),
    all:translate('common.allManufacturers'),
    language:translate('common.language'),
    backTop:translate('common.backTop'),
    protocol:translate('explorer.protocol'),
    previewDevice:translate('explorer.device'),
  });
  update();
  return subscribeLocale(update);
}
