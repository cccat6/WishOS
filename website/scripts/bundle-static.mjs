import {readdir,readFile,mkdir,writeFile} from 'node:fs/promises';
import {gzipSync} from 'node:zlib';
import {createHash} from 'node:crypto';
const root=new URL('../dist/',import.meta.url), files={};
const types={html:'text/html; charset=utf-8',css:'text/css; charset=utf-8',js:'text/javascript; charset=utf-8',json:'application/json',svg:'image/svg+xml',png:'image/png',jpg:'image/jpeg',xml:'application/xml',txt:'text/plain; charset=utf-8',zip:'application/zip'};
async function walk(path=''){for(const d of await readdir(new URL(path,root),{withFileTypes:true})){const key=path+d.name;if(d.isDirectory())await walk(key+'/');else if(!d.name.startsWith('_')){const bytes=await readFile(new URL(key,root));files['/'+key]={data:gzipSync(bytes).toString('base64'),type:types[key.split('.').at(-1)]||'application/octet-stream',etag:createHash('sha256').update(bytes).digest('hex')};}}}
await walk();
const source=`const files=${JSON.stringify(files)};\nexport default {async fetch(request){
 const url=new URL(request.url), path=url.pathname;
 if(request.method!=='GET'&&request.method!=='HEAD')return new Response('Method not allowed',{status:405,headers:{Allow:'GET, HEAD'}});
 let key=path.endsWith('/')?path+'index.html':path;
 if(!files[key]&&files[path+'/index.html'])return Response.redirect(url.origin+path+'/'+url.search,301);
 let item=files[key], status=200;if(!item){item=files['/404.html'];status=404;}
 const headers={'Content-Type':item.type,'Content-Encoding':'gzip','ETag':'"'+item.etag+'"','Cache-Control':item.type.startsWith('text/html')?'public, max-age=60':'public, max-age=3600','X-Content-Type-Options':'nosniff','Referrer-Policy':'strict-origin-when-cross-origin'};
 if(request.headers.get('If-None-Match')===headers.ETag)return new Response(null,{status:304,headers});
 return new Response(request.method==='HEAD'?null:Uint8Array.from(atob(item.data),c=>c.charCodeAt(0)),{status,headers,encodeBody:'manual'});
}};\n`;
const out=new URL('../build/static/',import.meta.url);await mkdir(out,{recursive:true});await writeFile(new URL('worker.mjs',out),source);await writeFile(new URL('wrangler.jsonc',out),JSON.stringify({name:'wishos-website',main:'worker.mjs',compatibility_date:'2026-10-20',workers_dev:false,routes:[{pattern:'wishos.dev',custom_domain:true}]},null,2));console.log(`Bundled ${Object.keys(files).length} static resources into ${Buffer.byteLength(source)} bytes`);
