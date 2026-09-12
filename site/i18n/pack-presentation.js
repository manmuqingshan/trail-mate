import {getLocale,translate} from './runtime.js';

// Release metadata describes device resources; its English prose is not website copy.
const describedPacks=new Set(['ar','europe-cyrillic-ext','europe-latin-ext','ja','ko','zh-Hans','zh-Hant']);
export function languageName(id) {
  const code=id==='zh-Hant'?'zh-Hant-TW':id;
  try {return new Intl.DisplayNames([getLocale()],{type:'language',fallback:'code'}).of(code);}
  catch {return id;}
}
export function packName(pack) {
  if(pack.id==='europe-cyrillic-ext'||pack.id==='europe-latin-ext')return translate(`pack.name.${pack.id}`);
  return languageName(pack.id);
}
export function packDescription(pack) {
  return translate(`pack.description.${describedPacks.has(pack.id)?pack.id:'generic'}`);
}
export function packCount(kind,count) {
  return translate(`pack.count.${kind}`,{count:new Intl.NumberFormat(getLocale()).format(count)});
}
export function packStatus(status) {
  return translate(status==='release'?'pack.release':'pack.review');
}
export function memoryProfile(profile) {
  return ['standard','extended','constrained'].includes(profile)?translate(`pack.memory.${profile}`):profile;
}
