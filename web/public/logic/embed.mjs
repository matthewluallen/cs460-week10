// Keep single-file/offline Logic Studio delivery while sharing the tested exporter.
import fs from 'node:fs';
const page=new URL('../logic_studio.html',import.meta.url);
const source=fs.readFileSync(new URL('./structured-text.mjs',import.meta.url),'utf8');
const parser=fs.readFileSync(new URL('./parse-structured-text.mjs',import.meta.url),'utf8').replace(/^import[^\n]*\n/,'').replace('export function parseStructuredText','function parseStructuredText');
const dependencies=fs.readFileSync(new URL('./dependencies.mjs',import.meta.url),'utf8').replace(/^import[^\n]*\n/,'').replaceAll('export function ','function ');
const start='<!-- ST-ADAPTER-BEGIN -->',end='<!-- ST-ADAPTER-END -->';
const block=start+'\n<script>\nwindow.StudioST = (() => {\n'+source.replace('export function exportStructuredText','function exportStructuredText')+'\n'+parser+'\n'+dependencies+'\nreturn {exportStructuredText,parseStructuredText,programDependencies,describeDependencySelection};\n})();\n</script>\n'+end;
let html=fs.readFileSync(page,'utf8');
if(html.includes(start))html=html.slice(0,html.indexOf(start))+block+html.slice(html.indexOf(end)+end.length);
else html=html.replace('<script>\n/* ===== opcodes',block+'\n<script>\n/* ===== opcodes');
if(process.argv.includes('--check')){
 if(html!==fs.readFileSync(page,'utf8'))throw Error('Embedded ST adapter is stale');
}else fs.writeFileSync(page,html);
