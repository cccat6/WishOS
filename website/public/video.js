export function initVideoCards() {
 const dialog = document.querySelector('[data-video-dialog]');
 if (!dialog) return;
 const player = dialog.querySelector('[data-video-player]');
 const empty = dialog.querySelector('[data-video-empty]');
 function stop() { player.pause(); player.removeAttribute('src'); player.load(); }
 document.querySelectorAll('[data-video-id]').forEach(button=>button.addEventListener('click',()=>{
   dialog.querySelector('#video-title').textContent = button.dataset.videoTitle || button.getAttribute('aria-label');
   const src = button.dataset.videoSrc;
   player.hidden = !src;
   empty.hidden = Boolean(src);
   if (src) player.src = src;
   dialog.showModal();
   if (src) player.play().catch(()=>{player.hidden=true;empty.hidden=false;});
 }));
 dialog.querySelector('[data-video-close]').addEventListener('click',()=>dialog.close());
 dialog.addEventListener('close',stop);
 window.addEventListener('pagehide',()=>{if (dialog.open) dialog.close();else stop();});
}
