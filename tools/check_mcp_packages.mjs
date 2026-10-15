// Integration peer for check_mcp_packages.py. It uses an explicitly selected Hub
// checkout and the real MCP client, never an experiment board or model account.
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';
import { join } from 'node:path';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { randomUUID, randomBytes } from 'node:crypto';
import { createInterface } from 'node:readline';
const lines=createInterface({input:process.stdin});
const input=JSON.parse((await lines[Symbol.asyncIterator]().next()).value);lines.close();
const require=createRequire(join(input.hub,'package.json'));
const {Client,StreamableHTTPClientTransport}=require('@modelcontextprotocol/client');
const {ConfigStore,token}=await import(pathToFileURL(join(input.hub,'dist/connect/config.js')));
const {ConnectorServer}=await import(pathToFileURL(join(input.hub,'dist/connect/server.js')));
const mode=input.mode,device='physical-'+randomUUID().slice(0,8),app='mcp-'+randomUUID().slice(0,8),report={mode,checks:[]},dirs=new Set();
const emit=(phase,data={})=>process.stdout.write(JSON.stringify({phase,...data})+'\n');
let server,root,owner,client,auth,base;
async function api(path,body,credential=owner,method){const response=await fetch(base+path,{method:method??(body===undefined?'GET':'POST'),headers:{'Content-Type':'application/json','User-Agent':'WishOS-device-integration/1',...(credential?{Authorization:'Bearer '+credential}:{})},body:body===undefined?undefined:JSON.stringify(body),signal:AbortSignal.timeout(25000)});const data=await response.json();if(!response.ok)throw Error(path+': '+response.status+' '+JSON.stringify(data));return data}
const parse=(r,allowError=false)=>{if(r.isError&&!allowError)throw Error(JSON.stringify(r.content));const text=r.content.find(c=>c.type==='text');if(!text)throw Error('No structured MCP tool reply');return JSON.parse(text.text)};
async function tool(name,args={},allowError=false){return parse(await client.callTool({name,arguments:args}),allowError)}
async function invoke(path,method='read',args={},allowError=false){const r=await tool('invoke_resource',{device_id:device,path,method,args,...(method==='read'||method==='describe'||method==='list'||method==='mounts'?{}:{expected_boot_id:report.boot_id,request_id:randomUUID()})},allowError);if(!r.ok&&!allowError)throw Error(path+':'+r.error);return r}
async function wait(fn,ms=45000){const until=Date.now()+ms;while(Date.now()<until){const r=await fn();if(r)return r;await new Promise(r=>setTimeout(r,50))}throw Error('MCP fixture deadline')}
async function job(name,args,failed=false){console.error('MCP package check:',name);const started=await tool(name,{...args,request_id:randomUUID()});const settled=await wait(async()=>{const r=await tool('read_resource_job',{id:started.id});return ['completed','failed','unknown'].includes(r.state)?r:false},120000);if(settled.receipts?.transfer?.root)dirs.add(settled.receipts.transfer.root);if(failed){if(settled.state!=='failed')throw Error('Invalid submission was not rejected: '+JSON.stringify(settled));return settled}if(settled.state!=='completed')throw Error(JSON.stringify(settled));return settled.result}
async function stage(path,text,kind){const artifact=await job('stage_resource',{text});return{path,artifact_id:artifact.id,...(kind?{kind}:{})}}
try{
 if(mode==='local'){
  root=await mkdtemp(join(tmpdir(),'wishos-mcp-packages-'));const store=new ConfigStore(root);const config=await store.load();const deviceToken=token();await store.save({...config,port:0,mdns:false,devices:[{id:device,label:'Temporary C3 package integration',token:deviceToken}]});server=new ConnectorServer(store,{mdns:false});const started=await server.start();base=new URL(started.url).origin;auth=config.localToken;
  emit('ready',{settings:{enabled:true,host:input.lan_host,port:Number(new URL(started.url).port),path:'/os/device?id='+device,tls:false,headers:{Authorization:'Bearer '+deviceToken}}});
 }else{
  base=input.cloud??'https://mcp.wishos.dev';const install=await api('/v1/installations',{id:randomUUID(),label:'Physical package validation',hostname:'wishos-unreachable-test.local',port:18762,pairing_seed:randomBytes(32).toString('base64url')},null);owner=install.owner_token;const profile=await api('/v1/devices',{id:device,label:'Temporary physical C3'},owner);auth=(await api('/v1/access',{},owner)).token;emit('ready',{settings:{enabled:true,endpoints:[profile.link.endpoints[1]]}});
 }
 client=new Client({name:'wishos-package-integration',version:'1'},{supportedProtocolVersions:['2026-07-28','2025-11-25'],versionNegotiation:{mode:{pin:'2026-07-28'}}});await client.connect(new StreamableHTTPClientTransport(new URL(base+'/mcp'),{requestInit:{headers:{Authorization:'Bearer '+auth}}}));
 const resources=await client.listResources();if(!resources.resources.some(r=>r.uri==='wishos://guidance/components'))throw Error('Guidance index unavailable');
 const index=await client.readResource({uri:'wishos://guidance/components'});const guides=JSON.parse(index.contents[0].text);for(const layer of ['system','drivers','applications','packages'])if(!guides.components.some(c=>c.layer===layer))throw Error('Missing guidance layer '+layer);
 const guide=await client.callTool({name:'get_guidance',arguments:{component_id:'wishos-packages'}});if(guide.isError)throw Error('Guide fallback failed');const prompts=await client.listPrompts();if(!prompts.prompts.some(p=>p.name==='wishos-authoring'))throw Error('Authoring prompt unavailable');report.checks.push('mcp_resources_prompt_and_skill_fallback');
 let board;
 await wait(async()=>{try{const r=await tool('invoke_resource',{device_id:device,path:'/sys/board',method:'read',args:{}});if(r.ok){board=r;return true}}catch{}return false},90000);if(board.result.mac!==input.serial_id)throw Error('Wrong physical USB identity');report.boot_id=board.boot_id;
 const selected=await tool('get_device_components',{device_id:device,expected_boot_id:report.boot_id});if(!JSON.stringify(selected).includes('headless-runtime'))throw Error('Selected components did not come from device');report.checks.push('real_c3_component_and_capability_discovery');
 if(input.probe){report.memory_before=(await invoke('/sys/resources')).result;report.stats_before=(await invoke('/sys/stats')).result;report.load_probe=await invoke('/sys/runtime','load',{source:'function on_start() marker="probe" end'},true);report.runtime_after=(await invoke('/sys/runtime')).result;report.memory_after=(await invoke('/sys/resources')).result;report.ok=true;throw Object.assign(Error('probe_complete'),{probe:true})}
 const before=(await invoke('/sys/transfers')).result.id,rev0=(await invoke('/sys/apps')).result.revision;
 const descriptor=await stage('app.json',JSON.stringify({format:'wishos-app/1',id:app,title:'Physical MCP test',entry:'main.lua',help:{controls:'Temporary fixture; use system home/USB control'}}),'json');
 const bad=await stage('main.lua','function on_start(');await job('prepare_package',{device_id:device,expected_boot_id:report.boot_id,files:[descriptor,bad]},true);
 if((await invoke('/sys/transfers')).result.id!==before||(await invoke('/sys/apps')).result.revision!==rev0)throw Error('Rejected preflight changed device');report.checks.push('bad_lua_rejected_before_transfer_begin');
 const code=await stage('main.lua','function on_start() marker="first" end');const asset=await stage('unchanged.bin','unchanged payload '.repeat(1024),'asset');const prepared=await job('prepare_package',{device_id:device,expected_boot_id:report.boot_id,files:[descriptor,code,asset]});
 const installed=await job('deploy_package',{device_id:device,expected_boot_id:report.boot_id,files:prepared.files,persist:true,activate:true});dirs.add(installed.package.slice(0,installed.package.lastIndexOf('/')));if(installed.state!=='activated')throw Error('Not activated');report.checks.push('mcp_prepare_native_validation_install_activate');
 const oldOwner=(await invoke('/sys/runtime')).result.owner;const revision=(await invoke('/sys/apps')).result.revision;const replacement=await stage('main.lua','function on_start() marker="second" end');
 const updated=await job('update_package',{device_id:device,expected_boot_id:report.boot_id,application_id:app,expected_revision:revision,replacements:[replacement],persist:true,activate:true});dirs.add(updated.package.slice(0,updated.package.lastIndexOf('/')));if(updated.state!=='activated'||(await invoke('/sys/runtime')).result.owner===oldOwner)throw Error('File update did not activate');report.checks.push('mcp_whole_file_update_with_native_local_copies');report.update={changed_files:updated.changed_files,copied_files:updated.copied_files};
 const active=(await invoke('/sys/runtime')).result.owner,now=(await invoke('/sys/apps')).result.revision,transfer=(await invoke('/sys/transfers')).result.id;
 const wrong=await stage('main.lua','local = 1');await job('update_package',{device_id:device,expected_boot_id:report.boot_id,application_id:app,expected_revision:now,replacements:[wrong],persist:true,activate:true},true);
 if((await invoke('/sys/runtime')).result.owner!==active||(await invoke('/sys/transfers')).result.id!==transfer)throw Error('Bad replacement changed current revision');report.checks.push('bad_replacement_leaves_running_revision');
 await job('update_package',{device_id:device,expected_boot_id:report.boot_id,application_id:app,expected_revision:revision,replacements:[replacement],persist:true,activate:true},true);if((await invoke('/sys/apps')).result.revision!==now)throw Error('Stale replacement committed');report.checks.push('stale_revision_is_not_merged_or_replayed');
 const denied=await invoke('/fs','write',{file:updated.manifest.slice(0,updated.manifest.lastIndexOf('/'))+'/main.lua',offset:'0',text:'bad',truncate:true},true);if(denied.ok||denied.error!=='file_in_use')throw Error('Active package can be overwritten');report.checks.push('mcp_filesystem_write_respects_native_revision_lease');
 report.native_stats=(await invoke('/sys/stats')).result;report.ok=true;
}catch(error){if(!error?.probe)report.error=error?.stack??String(error);try{if(report.boot_id){report.error_resources=(await invoke('/sys/resources')).result;report.error_stats=(await invoke('/sys/stats')).result;report.error_runtime=(await invoke('/sys/runtime')).result}}catch{}}
finally{
 if(client&&report.boot_id){try{await invoke('/sys/runtime','stop');const apps=(await invoke('/sys/apps')).result;if(apps.validation_operation_id)await invoke('/sys/apps','release_validation',{operation_id:apps.validation_operation_id});const t=(await invoke('/sys/transfers')).result;if(t.state!=='idle'&&t.state!=='aborted')await invoke('/sys/transfers','abort',{id:t.id});const removed=await invoke('/sys/apps','remove',{id:app,persist:true},true);if(!removed.ok&&removed.error!=='application_not_installed')throw Error('Fixture catalog cleanup: '+removed.error);
   for(const dir of dirs){for(const name of ['app.json','main.lua','unchanged.bin','files.jsonl','package.json']){const removed=await invoke('/fs','remove',{file:dir+'/'+name},true);if(!removed.ok&&removed.error!=='not_found')throw Error('Fixture file cleanup: '+removed.error)}const removed=await invoke('/fs','remove',{file:dir},true);if(!removed.ok&&removed.error!=='not_found')throw Error('Fixture directory cleanup: '+removed.error)}report.device_cleanup=true;
 }catch(error){report.cleanup_error=String(error)}}
 await client?.close().catch(()=>{});await server?.close().catch(()=>{});if(owner){try{report.cloud_cleanup=(await api('/v1/installations',undefined,owner,'DELETE')).removed}catch(error){report.cloud_cleanup_error=String(error)}}if(root)await rm(root,{recursive:true,force:true});emit('result',{report});
}
