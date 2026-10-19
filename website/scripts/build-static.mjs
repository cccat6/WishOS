import {spawnSync} from 'node:child_process';
const result=spawnSync(process.execPath,['scripts/build.mjs'],{cwd:new URL('..',import.meta.url),stdio:'inherit'});
if(result.status!==0)process.exit(result.status||1);
await import('./bundle-static.mjs');
