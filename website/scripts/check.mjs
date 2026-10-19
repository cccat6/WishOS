import assert from 'node:assert/strict';
import { readFile, stat, readdir } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { resolve } from 'node:path';
import { createHash } from 'node:crypto';
import { approvedBrand } from '../src/brand.mjs';

const dist = fileURLToPath(new URL('../dist/', import.meta.url));
async function htmlFiles(directory,prefix='') {
  const result=[];
  for (const entry of await readdir(directory,{withFileTypes:true})) {
    const path=prefix+entry.name;
    if (entry.isDirectory()) result.push(...await htmlFiles(resolve(directory,entry.name),path+'/'));
    else if (entry.name.endsWith('.html')) result.push(path);
  }
  return result;
}
const pages = await htmlFiles(dist);
const html = new Map(await Promise.all(pages.map(async path => [path, await readFile(resolve(dist,path),'utf8')])));
const idsFor = text => [...text.matchAll(/(?<![\w-])id="([^"]+)"/g)].map(match => match[1]);
let links = 0;
for (const [path, text] of html) {
  assert.ok(!text.includes('/brand/')&&!text.includes('data-param=')&&!text.includes('optical-method'),`${path}: internal brand study must not be published`);
  assert.ok(!/For fun|爱好者项目|待发布|待接入|素材待补|待用户审核|完整闭环仍在集成/.test(text),`${path}: internal project status must not appear in visitor copy`);
  // Catch known editorial regressions without banning necessary error or setup language.
  const prose=text.replace(/<script\b[^>]*>[\s\S]*?<\/script>/gi,'').replace(/<[^>]+>/g,' ');
  assert.ok(!/(?:不是|并非)[^。！？]{0,100}而是|not only[\s\S]{0,80}but (?:also|rather)|isn[’']t about[\s\S]{0,80}(?:it[’']s|it is) about/i.test(prose),`${path}: avoid defensive contrast copy`);
  assert.ok(!/Launcher 是默认应用，不是固定在系统里的界面|Launcher 和其他应用都能使用语音、存储与远程连接|应用由 WishOS 运行，(?:更新时|更新应用时|应用更新)/.test(prose),`${path}: removed explanatory copy returned`);
  const nav=text.match(/<nav id="primary-nav"[^>]*>([\s\S]*?)<\/nav>/)?.[1];
  if(nav)for(const match of nav.matchAll(/href="([^"]+)"/g))assert.match(match[1],/^\/(zh|en)\/(explore|system|hardware|opensource|docs|mcp|faq)\/$/,`${path}: navigation must open an independent page`);
  assert.equal((text.match(/<h1[ >]/g) || []).length, 1, `${path}: main heading`);
  const ids = idsFor(text);
  assert.equal(new Set(ids).size, ids.length, `${path}: duplicate IDs`);
  for (const attribute of ['aria-controls','aria-labelledby']) {
    for (const match of text.matchAll(new RegExp(`${attribute}="([^"]+)"`,'g'))) {
      for (const id of match[1].split(' ')) assert.ok(ids.includes(id), `${path}: missing ${attribute} target ${id}`);
    }
  }
  for (const match of text.matchAll(/\b(?:href|src)="([^"]+)"/g)) {
    const url = new URL(match[1], `https://preview.test/${path.replace(/index\.html$/, '')}`);
    if (url.origin !== 'https://preview.test') continue;
    const asset = decodeURIComponent(url.pathname).slice(1) + (url.pathname.endsWith('/') ? 'index.html' : '');
    assert.ok((await stat(resolve(dist, asset))).isFile(), `${path}: missing local asset ${asset}`);
    if (url.hash) {
      const targetHtml = html.get(asset) || await readFile(resolve(dist,asset),'utf8');
      assert.ok(idsFor(targetHtml).includes(url.hash.slice(1)), `${path}: broken anchor ${url.hash}`);
    }
    links++;
  }
}
const allowedRoots = new Set(['index.html','zh','en','404.html','app.js','games.js','game-engine.js','site.js','styles.css','_headers','robots.txt','sitemap.xml','loader.js','video.js','hardware.js','flash.js','releases.json','media',...approvedBrand.publicAssets]);
for (const file of await readdir(dist)) assert.ok(allowedRoots.has(file), `Unexpected publication asset: ${file}`);
for(const name of approvedBrand.publicAssets){
 const body=await readFile(resolve(dist,name));
 assert.equal(createHash('sha256').update(body).digest('hex'),approvedBrand.sha256[name],`${name}: website must use the frozen approved SVG`);
}
const sitemap=await readFile(resolve(dist,'sitemap.xml'),'utf8');
assert.ok(!sitemap.includes('/brand/'),'Internal brand route must not appear in sitemap');
const base = process.argv[2];
if (base) {
  const routes=pages.filter(p=>p!=='404.html').map(p=>'/'+p.replace(/index\.html$/,''));
  for (const path of [...routes,'/styles.css','/app.js','/games.js','/game-engine.js','/site.js','/favicon.svg','/logo.svg','/loading.svg','/loading-small.svg','/loader.js','/video.js','/hardware.js','/flash.js','/releases.json','/sitemap.xml','/robots.txt']) {
    const response = await fetch(new URL(path, base));
    assert.equal(response.status,200,`${path}: HTTP status`);
    assert.equal(response.headers.get('x-content-type-options'),'nosniff',`${path}: security header`);
    if (path.endsWith('/')) assert.ok(/<h1(?:\s|>)/.test(await response.text()), `${path}: page served`);
  }
  const response = await fetch(new URL('/missing-review-page',base));
  assert.equal(response.status,404,'Unknown route must return 404');
  assert.ok((await response.text()).includes('Page not found'),'Localized 404 content');
  for(const path of ['/zh/brand/','/en/brand/','/brand-geometry.js','/brand-preview.js','/internal/brand/approved.json']){
   assert.equal((await fetch(new URL(path,base))).status,404,`${path}: private design material must not be served`);
  }
}
console.log(`Verified ${pages.length} HTML pages, ${links} local links/assets, accessible references and publication boundary${base ? ', HTTP routes and headers' : ''}.`);
