import assert from 'node:assert/strict';
import {locales} from '../../site/i18n/registry.js';
import {FEATURES} from '../../site/explorer-data.js';
import {setLocale,getLocale,translate,localizeFeature} from '../../site/i18n/runtime.js';

// Exercise the same lazy-loading path used by the website, not just catalog imports.
globalThis.document={documentElement:{lang:'en',dir:'ltr'}};
for(const locale of locales) {
  await setLocale(locale.id);
  assert.equal(getLocale(),locale.id);
  assert.equal(document.documentElement.lang,locale.id);
  assert.equal(document.documentElement.dir,locale.direction);
  assert.ok(translate('release.ready',{tag:'v1.2.3'}).includes('v1.2.3'));
  for(const feature of FEATURES) {
    const text=localizeFeature(feature);
    for(const field of ['name','summary','detail','needs']) {
      assert.equal(typeof text[field],'string',`${locale.id}:${feature.id}:${field}`);
      assert.ok(text[field].trim(),`${locale.id}:${feature.id}:${field}`);
    }
    {
      const catalog=(await import(`../../site/i18n/features-${locale.id}.js`)).default;
      assert.deepEqual(Object.keys(catalog).sort(),FEATURES.map(item=>item.id).sort());
      for(const field of ['name','summary','detail','needs'])assert.ok(catalog[feature.id][field]?.trim());
      if(locale.id==='zh-Hant-TW')assert.ok(catalog[feature.id].name,'Taiwan names must not fall back to Simplified Chinese');
    }
  }
}
// Last selection wins even when catalogs are loaded asynchronously.
await Promise.all([setLocale('ar'),setLocale('ja'),setLocale('en')]);
assert.equal(getLocale(),'en');
assert.equal(document.documentElement.dir,'ltr');
delete globalThis.document;
console.log(`${locales.length} locales: runtime loading, feature coverage, interpolation, direction and rapid switching passed.`);
