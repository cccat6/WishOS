import {harnessBody,apiBody,faqBody} from '../src/guides.mjs';
import {cp,mkdir,rm,writeFile,readFile} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import {content} from '../src/content.mjs';
import {approvedBrand,assetPath,completeKeyframes} from '../src/brand.mjs';
import {hardwarePage,flashPage} from '../src/pages.mjs';
import {homeBody,exploreBody,systemBody,openSourceBody,docsBody,mcpBody} from '../src/site-pages.mjs';
import {linkAttrs} from '../src/links.mjs';
const root=fileURLToPath(new URL('../',import.meta.url)),output=`${root}dist`,origin='https://wishos.dev';
const e=value=>String(value).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
function head(c,l,missing=false,subpath='',script=null){return `<!doctype html><html lang="${c.lang}"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>${e(missing?`${c.missing} · WishOS`:c.title)}</title><meta name="description" content="${e(c.description)}">${missing?'<meta name="robots" content="noindex">':`<link rel="canonical" href="${origin}/${l}/${subpath}"><link rel="alternate" hreflang="zh-CN" href="${origin}/zh/${subpath}"><link rel="alternate" hreflang="en" href="${origin}/en/${subpath}"><link rel="alternate" hreflang="x-default" href="${origin}/zh/${subpath}"><meta property="og:title" content="${e(c.title)}"><meta property="og:description" content="${e(c.description)}"><meta property="og:type" content="website"><meta property="og:url" content="${origin}/${l}/${subpath}">`}<meta name="theme-color" content="#f7f5ef"><link rel="icon" href="/favicon.svg" type="image/svg+xml"><link rel="stylesheet" href="/styles.css">${missing?'':'<script type="module" src="/site.js"></script>'}${script?`<script type="module" src="${script}"></script>`:''}</head>`;}
function brandLockup(l,kind='header'){const size=kind==='footer'?38:48;return `<a class="brand brand--${kind}" href="/${l}/" aria-label="WishOS home"><img src="/logo-${size}.svg" alt="" width="${size}" height="${size}"><span class="brand-type"><span class="brand-wordmark">WishOS</span><span class="brand-slogan" lang="en">${approvedBrand.slogan}</span></span></a>`;}
function header(c,l,inner=false,route=''){return `<header class="header" data-menu-open="false">${brandLockup(l)}<nav id="primary-nav" aria-label="${c.navLabel}">${c.nav.map(([path,label])=>`<a href="/${l}/${path}/"${(route===path||(['connect','api'].includes(route)&&path==='docs'))?' aria-current="page"':''}>${label}</a>`).join('')}</nav><div class="header-actions"><a class="language" href="/${c.other}/${route?`${route}/`:''}" lang="${content[c.other].lang}">${c.language}</a><button class="menu-toggle" data-menu-toggle aria-expanded="false" aria-controls="primary-nav" aria-label="${l==='zh'?'打开导航菜单':'Open navigation menu'}"><span aria-hidden="true">☰</span></button></div></header>`;}
function footer(c,l){return `<footer class="site-footer footer-simple"><small>© 2026 WishOS</small></footer>`;}

function full(c,l,route,body,script=null,title=null){return `${head(title?{...c,title:`${title} · WishOS`}:c,l,false,route?route+'/':'',script)}<body><a class="skip" href="#main">${c.skip}</a>${header(c,l,Boolean(route),route)}<main id="main" class="${route?'inner-page':'home-page'}">${body}</main>${footer(c,l)}</body></html>`;}
const routes=[['explore',exploreBody,'/app.js'],['system',systemBody,null],['opensource',openSourceBody,null],['docs',docsBody,null],['mcp',mcpBody,null],['connect',harnessBody,null],['api',apiBody,null],['faq',faqBody,null]];
await rm(output,{recursive:true,force:true});await mkdir(output,{recursive:true});await cp(`${root}public`,output,{recursive:true});
for(const name of approvedBrand.publicAssets)await cp(assetPath(name),`${output}/${name}`);
await writeFile(`${output}/styles.css`,await readFile(`${root}public/styles.css`,'utf8')+'\n'+completeKeyframes);
for(const [l,c]of Object.entries(content)){
 await mkdir(`${output}/${l}`,{recursive:true});await writeFile(`${output}/${l}/index.html`,full(c,l,'',homeBody(c,l),'/app.js'));
 for(const [route,render,script]of routes){await mkdir(`${output}/${l}/${route}`,{recursive:true});await writeFile(`${output}/${l}/${route}/index.html`,full(c,l,route,render(c,l),script,c.nav.find(([id])=>id===route)?.[1]??({connect:l==='zh'?'Harness 接入教程':'Harness connection guide',api:l==='zh'?'API 接入教程':'API connection guide'}[route])));}
 for(const [route,render]of [['hardware',hardwarePage],['flash',flashPage]]){await mkdir(`${output}/${l}/${route}`,{recursive:true});await writeFile(`${output}/${l}/${route}/index.html`,render(c,l,head,header,footer));}
}
await writeFile(`${output}/index.html`,full(content.zh,'zh','',homeBody(content.zh,'zh'),'/app.js'));
await writeFile(`${output}/404.html`,`${head(content.en,'en',true)}<body><main class="not-found">${brandLockup('en')}<h1>404</h1><p>页面不存在 · Page not found</p><a href="/zh/">返回首页</a> · <a href="/en/">Back to home</a></main></body></html>`);
await writeFile(`${output}/sitemap.xml`,`<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">${Object.keys(content).flatMap(l=>['',...routes.map(([r])=>r+'/'),'hardware/','flash/'].map(r=>`<url><loc>${origin}/${l}/${r}</loc></url>`)).join('')}</urlset>\n`);
await writeFile(`${output}/robots.txt`,`User-agent: *\nAllow: /\nSitemap: ${origin}/sitemap.xml\n`);
console.log('Built WishOS website: independent bilingual pages, interactive experiences and frozen SVG identity.');
