import { initVideoCards } from './video.js';
initVideoCards();
const rows = [...document.querySelectorAll('.directory-row')];
const search = document.querySelector('[data-hardware-search]');
const filter = document.querySelector('[data-hardware-filter]');
const chinese = document.documentElement.lang.startsWith('zh');
function update() {
 const query = search.value.trim().toLowerCase();
 let count = 0;
 rows.forEach(row=>{
   row.hidden = !(row.dataset.search.includes(query) && (filter.value === 'all' || row.dataset.kind === filter.value));
   if (!row.hidden) count++;
 });
 document.querySelector('[data-result-count]').textContent = chinese ? `显示 ${count} 个设备条目` : `${count} device entries`;
 document.querySelector('[data-no-results]').hidden = count > 0;
}
search.addEventListener('input',update);
filter.addEventListener('change',update);
update();
