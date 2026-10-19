const header=document.querySelector('.header');
const menu=document.querySelector('[data-menu-toggle]');
if(header&&menu){
 const setOpen=open=>{header.dataset.menuOpen=String(open);menu.setAttribute('aria-expanded',String(open));};
 menu.addEventListener('click',()=>setOpen(menu.getAttribute('aria-expanded')!=='true'));
 header.querySelectorAll('nav a').forEach(a=>a.addEventListener('click',()=>setOpen(false)));
 document.addEventListener('keydown',event=>{if(event.key==='Escape'&&menu.getAttribute('aria-expanded')==='true'){setOpen(false);menu.focus();}});
 matchMedia('(min-width:901px)').addEventListener('change',()=>setOpen(false));
}
// Project-specific destinations are supplied in src/links.mjs.
// Until substituted, keep placeholder links from changing the preview's scroll position.
document.querySelectorAll('[data-link-placeholder]').forEach(link=>link.addEventListener('click',event=>event.preventDefault()));
// A single shared connection block with an optional, pausable name rotation.
document.querySelectorAll('[data-rotation-toggle]').forEach(button=>button.addEventListener('click',()=>{
 const paused=button.getAttribute('aria-pressed')!=='true';button.setAttribute('aria-pressed',String(paused));button.closest('.connect-block').dataset.rotationPaused=String(paused);button.textContent=paused?button.dataset.resume:button.dataset.pause;button.setAttribute('aria-label',button.textContent);
}));
const sourceMenu=document.querySelector('[data-source-menu]');
if(sourceMenu){
 document.addEventListener('click',event=>{if(!sourceMenu.contains(event.target))sourceMenu.open=false;});
 document.addEventListener('keydown',event=>{if(event.key==='Escape'&&sourceMenu.open){sourceMenu.open=false;sourceMenu.querySelector('summary').focus();}});
}
