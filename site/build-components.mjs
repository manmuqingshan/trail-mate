import {build} from 'esbuild';
import {readdir,readFile,writeFile} from 'node:fs/promises';

await build({
  entryPoints:['components.jsx'],bundle:true,minify:true,format:'esm',
  outdir:'components-dist',legalComments:'linked',
  loader:{'.woff2':'file','.woff':'file','.ttf':'file','.svg':'file','.png':'file'},
});
// Preserve every third-party notice; normalize indentation for the repository.
for(const file of await readdir('components-dist')) {
  if(!file.endsWith('.LEGAL.txt'))continue;
  const path=`components-dist/${file}`;
  await writeFile(path,(await readFile(path,'utf8')).replaceAll('\t','  '));
}
console.log('Animal Island UI bundle built; third-party license notices retained.');
