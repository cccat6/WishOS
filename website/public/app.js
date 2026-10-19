import {createWishLoader} from './loader.js';
import {initVideoCards} from './video.js';
import {mountGame} from './games.js';
initVideoCards();
const tabs=[...document.querySelectorAll('[data-tab]')],panels=[...document.querySelectorAll('.showcase-panel')];
const examples=JSON.parse(document.querySelector('[data-wish-examples]').textContent);
const intent=document.querySelector('[data-wish-intent]'),run=document.querySelector('[data-run-wish]'),process=document.querySelector('.wish-process'),status=document.querySelector('[data-wish-status]');
const position=document.querySelector('[data-wish-position]'),caption=document.querySelector('[data-wish-caption]');
const loader=createWishLoader(document.querySelector('[data-wish-task]'));
const zh=document.documentElement.lang.startsWith('zh'),t=(a,b)=>zh?a:b;
const cursors=Object.fromEntries(Object.keys(examples).map(id=>[id,0])),receipts={};
let current='ui',wishTicker=null,game=null;
function stopWish(){if(wishTicker!==null){clearInterval(wishTicker);wishTicker=null;}loader.reset();process.hidden=true;run.disabled=false;}
function prompt(){const index=cursors[current];intent.textContent=examples[current][index].wish;position.textContent=`${String(index+1).padStart(2,'0')} / ${String(examples[current].length).padStart(2,'0')}`;caption.textContent=receipts[current]?t('再许一个愿望','Make another wish'):t('试试这个愿望','Try this wish');if(receipts[current]){process.hidden=false;loader.complete();status.textContent=receipts[current];}}
function selectTab(id,focus=false){
 if(!tabs.some(tab=>tab.dataset.tab===id))return;
 stopWish();if(id!=='games')game?.pause();current=id;
 tabs.forEach(tab=>{const active=tab.dataset.tab===id;tab.setAttribute('aria-selected',String(active));tab.tabIndex=active?0:-1;if(active&&focus)tab.focus();});
 panels.forEach(panel=>{panel.hidden=panel.id!==`panel-${id}`;});prompt();
}
tabs.forEach((tab,i)=>{tab.addEventListener('click',()=>selectTab(tab.dataset.tab));tab.addEventListener('keydown',event=>{let index;if(event.key==='ArrowRight')index=(i+1)%tabs.length;if(event.key==='ArrowLeft')index=(i+tabs.length-1)%tabs.length;if(event.key==='Home')index=0;if(event.key==='End')index=tabs.length-1;if(index!==undefined){event.preventDefault();selectTab(tabs[index].dataset.tab,true);}});});
document.querySelectorAll('[data-demo-link]').forEach(link=>link.addEventListener('click',()=>selectTab(link.dataset.demoLink)));
const initial=new URLSearchParams(location.search).get('view');if(initial)selectTab(initial==='timer'?'games':initial);
// Delegated controls survive replacement of a scene. Their effects stay in this preview.
document.querySelector('.showcase-screen').addEventListener('click',event=>{
 const button=event.target.closest('button');if(!button)return;
 const host=button.closest('[data-scene-host]');
 if(button.matches('[data-layout-toggle]'))host.querySelector('[data-dashboard]').classList.toggle('layout-minimal');
 if(button.matches('[data-context]')){host.querySelectorAll('[data-context]').forEach(b=>b.setAttribute('aria-pressed',String(b===button)));host.querySelectorAll('[data-context-scene]').forEach(p=>p.hidden=p.dataset.contextScene!==button.dataset.context);}
 if(button.matches('[data-art-toggle]'))host.querySelector('[data-art]').classList.toggle('art-cool');
 if(button.matches('[data-chat-mode]')){host.querySelectorAll('[data-chat-mode]').forEach(b=>b.setAttribute('aria-pressed',String(b===button)));host.querySelectorAll('[data-chat-view]').forEach(p=>p.hidden=p.dataset.chatView!==button.dataset.chatMode);}
 if(button.matches('[data-reader-next]')){const text=host.querySelector('[data-reader-text]'),next=text.dataset.next;text.dataset.next=text.textContent;text.textContent=next;const page=host.querySelector('[data-reader-page]');page.textContent=page.textContent==='12'?'13':'12';}
});
document.querySelector('.showcase-screen').addEventListener('change',event=>{const list=event.target.closest('[data-checklist]');if(!list)return;const left=list.querySelectorAll('input:not(:checked)').length;list.querySelector('[data-checklist-status]').textContent=left?t(`还有 ${left} 件，逐项确认。`,`${left} things left. Check them off.`):t('都带齐了，可以出发！','All packed. Ready to go!');});
run.addEventListener('click',()=>{
 if(wishTicker!==null)return;loader.reset();process.hidden=false;status.textContent=status.dataset.playing;run.disabled=true;const target=current,scene=examples[target][cursors[target]],began=performance.now();
 // Timed illustration only. Completion reflects a rendered scene, not a remote operation.
 wishTicker=setInterval(()=>{const progress=Math.min(100,(performance.now()-began)/700*100);loader.setProgress(progress);if(progress<100)return;clearInterval(wishTicker);wishTicker=null;
  if(target==='games')game?.destroy();
  const host=document.querySelector(`[data-scene-host="${target}"]`);host.innerHTML=scene.html;host.classList.remove('scene-enter');void host.offsetWidth;host.classList.add('scene-enter');
  if(target==='games')game=mountGame(host.querySelector('[data-game]'),zh);
  receipts[target]=scene.receipt;cursors[target]=(cursors[target]+1)%examples[target].length;loader.complete();prompt();run.disabled=false;
 },25);
});
window.addEventListener('pagehide',event=>{game?.pause();stopWish();if(!event.persisted){game?.destroy();loader.destroy();}});
window.addEventListener('pageshow',event=>{if(event.persisted)prompt();});
