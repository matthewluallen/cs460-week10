// Compile a Studio .csco project to the exact TBD-PROJECT payload the Studio loads with
// PLCLOAD, using the compiler embedded in web/public/logic_studio.html (no browser).
//
//   node tools/compile-project.mjs web/public/projects/motor-start-stop-estop.csco
//       -> prints the payload, its instruction count and sha256 (= `project json` payload_sha256)
//   node tools/compile-project.mjs <in.csco> --write sbom/firmware/factory/default-project.json
//   node tools/compile-project.mjs <in.csco> --check sbom/firmware/factory/default-project.json
//
// The factory/twin images seed sbom/firmware/factory/default-project.json into NVS
// (sbom/firmware/scripts/factory_image.py). --check proves those bytes are what the Studio
// would load for the .csco, so the controller's payload_sha256 matches the Studio's.
import fs from 'node:fs';
import vm from 'node:vm';
import crypto from 'node:crypto';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const html = fs.readFileSync(path.join(root, 'web/public/logic_studio.html'), 'utf8');
const block = (a, b) => {
  const i = html.indexOf(a), j = html.indexOf(b);
  if (i < 0 || j < 0) throw Error('Studio adapter markers not found: ' + a);
  return html.slice(i + a.length, j).replace(/<\/?script>/g, '');
};
const ctx = {window: {}, TextEncoder, console};
ctx.globalThis = ctx;
vm.createContext(ctx);
vm.runInContext(block('<!-- PROJECT-ADAPTER-BEGIN -->', '<!-- PROJECT-ADAPTER-END -->'), ctx);
const P = ctx.window.StudioProject;

const [input, mode, target] = process.argv.slice(2);
if (!input) { console.error('usage: node tools/compile-project.mjs <project.csco> [--write|--check <payload.json>]'); process.exit(2); }
const {project} = P.parseProject(fs.readFileSync(input, 'utf8'));
const text = P.deploymentText(project);
const payload = JSON.parse(text);
const sha = crypto.createHash('sha256').update(text).digest('hex');
console.log(`${project.id}: ${payload.code.length} instructions, ${Buffer.byteLength(text)} bytes, sha256 ${sha}`);
if (mode === '--write') { fs.writeFileSync(target, text); console.log('wrote ' + target); }
else if (mode === '--check') {
  const have = fs.readFileSync(target, 'utf8');
  if (have !== text) { console.error(`STALE: ${target} differs from the Studio compile of ${input}; rerun with --write and rebuild the images`); process.exit(1); }
  console.log('OK: ' + target + ' matches the Studio compile');
} else console.log(text);
