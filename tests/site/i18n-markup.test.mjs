import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import en from '../../site/i18n/en.js';
import {locales} from '../../site/i18n/registry.js';

const html=await readFile(new URL('../../site/index.html',import.meta.url),'utf8');
const keys=[...html.matchAll(/data-i18n(?:-aria|-alt|-placeholder)?="([^"]+)"/g)].map(match=>match[1]);
for(const {id:language} of locales) {
  const catalog=(await import(`../../site/i18n/${language}.js`)).default;
  for(const key of keys)assert.equal(typeof catalog[key],'string',`${language}: missing markup key ${key}`);
  for(const key of Object.keys(en)) {
    const tokens=value=>[...value.matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort();
    assert.deepEqual(tokens(catalog[key]),tokens(en[key]),`${language}: interpolation mismatch in ${key}`);
  }
}
console.log(`${keys.length} localized markup references and interpolation parameters verified.`);
