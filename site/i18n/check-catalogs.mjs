import {locales} from './registry.js';
import base from './en.js';
const errors=[];
const parameters=text=>[...text.matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort().join(',');
for(const locale of locales) {
  let catalog;
  try {catalog=(await import(`./${locale.id}.js`)).default;}
  catch(error){errors.push(`${locale.id}: catalog cannot be loaded (${error.code||error.message})`);continue;}
  for(const [key,value] of Object.entries(base)) {
    if(typeof catalog[key]!=='string'||!catalog[key].trim())errors.push(`${locale.id}: missing ${key}`);
    else if(parameters(value)!==parameters(catalog[key]))errors.push(`${locale.id}: parameters differ for ${key}`);
  }
  for(const key of Object.keys(catalog))if(!Object.hasOwn(base,key))errors.push(`${locale.id}: unknown ${key}`);
}
if(errors.length){console.error(errors.join('\n'));process.exitCode=1;}
else console.log(`${locales.length} website catalogs: keys, nonempty strings and parameters verified.`);
