import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {locales} from '../../site/i18n/registry.js';
import {setLocale,translate} from '../../site/i18n/runtime.js';
import {packName,packDescription,packCount,packStatus,languageName,memoryProfile} from '../../site/i18n/pack-presentation.js';

const catalog=JSON.parse(await readFile(new URL('../../site/data/packs.json',import.meta.url),'utf8'));
for(const locale of locales) {
  await setLocale(locale.id);
  for(const pack of catalog.packages.filter(pack=>pack.package_type==='locale-bundle')) {
    assert.ok(packName(pack));
    assert.equal(packDescription(pack),translate(`pack.description.${pack.id}`),'Published packs require a reviewed description, not the generic fallback');
    for(const language of pack.provides.locales)assert.ok(languageName(language.id));
    for(const profile of pack.supported_memory_profiles)assert.notEqual(memoryProfile(profile),profile);
  }
  assert.notEqual(packStatus('review'),'review');
  assert.notEqual(packStatus('release'),'release');
  for(const count of [0,1,2,3,11,21,100]) {
    const formatted=new Intl.NumberFormat(locale.id).format(count);
    for(const kind of ['locales','fonts','ime','packages','more'])assert.ok(packCount(kind,count).includes(formatted));
  }
}
console.log('Published firmware pack descriptions, localized language names, statuses, memory profiles and counts passed in all 11 locales.');
