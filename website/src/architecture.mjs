const t=(l,a,b)=>l==='zh'?a:b;
const paths={
 cloud:'M7 17H6a4 4 0 0 1-.5-8 6.5 6.5 0 0 1 12-1A4.5 4.5 0 0 1 18 17h-1M9 21v-8m-3 3 3-3 3 3m3-3v8m-3-3 3 3 3-3',
 host:'M3 4h18v13H3zM8 21h8m-4-4v4',
 ai:'M12 2l2.5 7.5L22 12l-7.5 2.5L12 22l-2.5-7.5L2 12l7.5-2.5Z',
 link:'M10 13a4 4 0 0 0 6 0l4-4a4 4 0 0 0-6-6l-2 2M14 11a4 4 0 0 0-6 0l-4 4a4 4 0 0 0 6 6l2-2',
 chat:'M3 4h18v13H9l-6 4V4Zm4 5h10m-10 4h7',
 home:'M3 10l9-7 9 7v11h-6v-7H9v7H3V10Z',
 apps:'M3 3h7v7H3zM14 3h7v7h-7zM3 14h7v7H3zM14 14h7v7h-7z',
 book:'M12 5C9 3 5 3 2 4v16c3-1 7-1 10 1 3-2 7-2 10-1V4c-3-1-7-1-10 1Zm0 0v16',
 game:'M7 7h10c3 0 4 2 4 5l1 6c0 3-3 3-4 1l-2-3H8l-2 3c-1 2-4 2-4-1l1-6c0-3 1-5 4-5Zm0 3v5m-2-2.5h4m7-1.5h.1m2 3h.1',
 clock:'M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18Zm0 4v5l4 2',
 display:'M3 3h18v14H3zM7 21h10m-5-4v4',
 mic:'M9 5a3 3 0 0 1 6 0v7a3 3 0 0 1-6 0V5Zm-3 6v1a6 6 0 0 0 12 0v-1M12 18v4m-3 0h6',
 folder:'M2 6h8l2 3h10v12H2V6Zm0 0V3h8l2 3h8v3',
 chip:'M6 6h12v12H6zM9 9h6v6H9zM9 2v4m6-4v4M9 18v4m6-4v4M2 9h4m-4 6h4m12-6h4m-4 6h4',
 input:'M4 4h16v16H4zM8 9h8m-4-4v8m-4 3h.1m8 0h.1',
 sensor:'M12 9a3 3 0 1 0 0 6 3 3 0 0 0 0-6ZM7 6a8 8 0 0 0 0 12M17 6a8 8 0 0 1 0 12M4 3a12 12 0 0 0 0 18M20 3a12 12 0 0 1 0 18',
 wifi:'M2 8a16 16 0 0 1 20 0M5 12a11 11 0 0 1 14 0M8 16a6 6 0 0 1 8 0M12 20h.1',
 back:'M9 5 2 12l7 7M2 12h12a7 7 0 0 1 7 7',
 settings:'M4 6h16M4 12h16M4 18h16M8 3v6m8 0v6M10 15v6'
};
const icon=(name,cls='')=>`<svg class="arch-icon ${cls}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.55" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="${paths[name]}"/></svg>`;
const tile=(name,label,detail='')=>`<div class="arch-tile">${icon(name)}<strong>${label}</strong>${detail?`<small>${detail}</small>`:''}</div>`;
export function architecture(l){return `<section class="home-architecture" aria-labelledby="architecture-title"><div class="architecture-heading"><h2 id="architecture-title">${t(l,'WishOS 系统架构','WishOS architecture')}</h2><p>${t(l,'AI 在云端或你的主机上处理任务，WishOS 在设备上运行应用。','AI handles tasks in the cloud or on your host. WishOS runs applications on the device.')}</p></div>
<div class="arch-board" role="group" aria-label="${t(l,'云端或主机与设备端的架构关系','Cloud or host and device architecture')}">
 <section class="arch-zone arch-cloud"><header class="arch-zone-heading"><span class="arch-zone-symbol">${icon('cloud')}${icon('host')}</span><h3>${t(l,'云端或你的主机','Cloud or your host')}</h3></header><article class="arch-ai-node"><div class="arch-node-heading">${icon('ai')}<h4>AI / Harness</h4></div><p>${t(l,'理解需求 · 生成应用 · 执行任务','Understand requests · Build apps · Run tasks')}</p><div class="arch-ai-options"><span>${t(l,'使用已有 Harness','Use your Harness')}</span><span>${t(l,'连接模型 API','Connect a model API')}</span></div></article><div class="arch-line" aria-hidden="true">↕</div><div class="arch-cloud-tools"><article>${icon('link')}<h4>${t(l,'MCP 服务','MCP service')}</h4><p>${t(l,'让 AI 调用设备工具','Tools for AI to use your device')}</p></article><article>${icon('chat')}<h4>${t(l,'连接服务','Connection service')}</h4><p>${t(l,'语音转写 · 会话连接','Transcription · Session routing')}</p></article></div><p class="arch-cloud-note">${t(l,'官方服务 / 自行部署','Official services / Self-hosted')}</p><div class="arch-traffic"><div><span>${t(l,'下发','To device')}</span>${t(l,'应用文件 · 操作指令','App files · Tool calls')}<b aria-hidden="true">→</b></div><div><span>${t(l,'回传','From device')}</span>${t(l,'语音请求 · 运行结果','Voice requests · Results')}<b aria-hidden="true">←</b></div></div></section>
 <section class="arch-zone arch-device"><header class="arch-zone-heading"><span class="arch-zone-symbol">${icon('chip')}</span><h3>${t(l,'设备端','Device')}</h3></header><div class="arch-app-layer"><div class="arch-layer-caption"><h4>${t(l,'运行时应用','Runtime applications')}</h4><span>${t(l,'可以保存、打开和替换','Save, launch and replace')}</span></div><div class="arch-app-grid"><article class="arch-launcher"><div class="arch-node-heading">${icon('home')}<h5>Launcher <span>${t(l,'启动器','')}</span></h5></div><p>${t(l,'默认桌面、基础操作与应用入口','Home screen, basic controls and app access')}</p></article><div class="arch-other-apps">${tile('book',t(l,'阅读器','Reader'))}${tile('game',t(l,'小游戏','Games'))}${tile('apps',t(l,'自定义应用','Your apps'))}</div></div></div><div class="arch-layer-arrow"><span>${t(l,'共同调用系统能力','Shared system capabilities')}</span><b aria-hidden="true">↓</b></div><article class="arch-system"><div class="arch-layer-caption"><h4>WishOS</h4><span>${t(l,'系统服务','System services')}</span></div><div class="arch-system-grid">${[
 ['apps',t(l,'应用管理','App lifecycle'),t(l,'打开 · 退出 · 资源回收','Launch · Exit · Cleanup')],
 ['display',t(l,'界面显示','Display'),t(l,'绘制 · 刷新 · 切换','Render · Refresh · Switch')],
 ['mic',t(l,'语音输入','Voice input'),t(l,'录音 · 编码 · 传输','Capture · Encode · Transfer')],
 ['link',t(l,'远程连接','Remote connection'),t(l,'连接维护 · 消息收发','Connection · Messages')],
 ['folder',t(l,'文件存储','Files'),t(l,'应用 · 素材 · 配置','Apps · Assets · Settings')],
 ['clock',t(l,'事件与定时','Events & timers'),t(l,'输入事件 · 自动化','Input events · Automation')]
 ].map(v=>tile(...v)).join('')}</div></article><div class="arch-layer-arrow"><span>${t(l,'通过驱动使用硬件','Access hardware through drivers')}</span><b aria-hidden="true">↓</b></div><article class="arch-drivers"><div class="arch-layer-caption"><h4>${t(l,'硬件驱动','Hardware drivers')}</h4><span>${t(l,'按具体外设适配','Adapted to each peripheral')}</span></div><div class="arch-driver-grid">${[['display',t(l,'屏幕','Screen')],['input',t(l,'按键 / 触摸','Keys / touch')],['mic',t(l,'音频','Audio')],['folder',t(l,'存储','Storage')],['wifi',t(l,'网络','Network')],['sensor',t(l,'传感器','Sensors')]].map(v=>tile(...v)).join('')}</div></article></section>
</div></section>`;}
