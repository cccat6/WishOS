const picker=document.querySelector('[data-flash-device]');
const scope=document.querySelector('[data-flash-scope]');
function update(){const name=picker.selectedOptions[0].textContent;scope.textContent=document.documentElement.lang.startsWith('zh')?`请核对设备型号：${name}。`:`Check your device model: ${name}.`;}
picker.addEventListener('change',update);update();
