import {locales} from './registry.js';

const aliases = {zh:'zh-Hans','zh-CN':'zh-Hans','zh-TW':'zh-Hant-TW','zh-Hant':'zh-Hant-TW'};
const catalogs = new Map();
const featureCatalogs = new Map();
const listeners = new Set();
let current = 'en';
let requestVersion = 0;

export function normalizeLocale(id) {
  const candidate=aliases[id] || id;
  return locales.some(locale=>locale.id===candidate)?candidate:'en';
}

export async function loadLocale(id) {
  const locale=normalizeLocale(id);
  if(!catalogs.has(locale)) {
    const module=await import(`./${locale}.js`);
    catalogs.set(locale,module.default);
  }
  if(!featureCatalogs.has(locale)) {
    featureCatalogs.set(locale,(await import(`./features-${locale}.js`)).default);
  }
  return catalogs.get(locale);
}

export async function setLocale(id) {
  const version=++requestVersion;
  const locale=normalizeLocale(id);
  await loadLocale(locale);
  if(version!==requestVersion)return;
  current=locale;
  if(typeof document!=='undefined') {
    document.documentElement.lang=locale;
    document.documentElement.dir=locales.find(item=>item.id===locale).direction;
  }
  for(const listener of listeners)listener(locale);
}

export function getLocale() { return current; }
export function localizeFeature(base) {
  const text=featureCatalogs.get(current)?.[base.id];
  if(!text)throw new Error(`Missing feature translation: ${current}:${base.id}`);
  return {...base,...text};
}
export function subscribeLocale(listener) {
  listeners.add(listener);
  return ()=>listeners.delete(listener);
}
export function translate(key, values={}) {
  const template=catalogs.get(current)?.[key];
  if(typeof template!=='string')throw new Error(`Missing website translation: ${current}:${key}`);
  return template.replace(/\{([\w]+)\}/g,(token,name)=>Object.hasOwn(values,name)?String(values[name]):token);
}
