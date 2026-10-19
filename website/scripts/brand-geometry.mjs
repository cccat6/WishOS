// Locally authored geometry. Internal export only; production consumes frozen SVG assets.
// Color math follows the published Oklab definitions; no icon asset is imported.
export const PHI=(1+Math.sqrt(5))/2;
export const STROKE_GAIN=.92;
export const SCHEMA={
 opening:{min:84,max:120,step:2,unit:'°',zh:'手腕开角',en:'Wrist opening'},
 handScale:{min:.85,max:1.18,step:.01,unit:'×',zh:'手势尺寸',en:'Hand scale'},
 handStroke:{min:7,max:12,step:.5,unit:'u',zh:'手势线宽',en:'Hand stroke'},
 starRatio:{min:.26,max:.52,step:.01,unit:'×',zh:'星／手高度',en:'Star / hand height'},
 ringGap:{min:10,max:28,step:1,unit:'u',zh:'圆环净距',en:'Ring clearance'},
 ringStroke:{min:9,max:16,step:.5,unit:'u',zh:'进度线宽',en:'Progress stroke'},
 lightness:{min:.56,max:.73,step:.005,unit:'L',zh:'暖橙明度',en:'Orange lightness'},
 chroma:{min:.09,max:.19,step:.005,unit:'C',zh:'暖橙色度',en:'Orange chroma'},
 hue:{min:42,max:64,step:1,unit:'°',zh:'暖橙色相',en:'Orange hue'}
};
export function hexToOklch(hex){
 const rgb=hex.match(/[a-f\d]{2}/gi).map(x=>parseInt(x,16)/255).map(x=>x<=.04045?x/12.92:((x+.055)/1.055)**2.4);
 const [r,g,b]=rgb;
 const l=Math.cbrt(.4122214708*r+.5363325363*g+.0514459929*b),m=Math.cbrt(.2119034982*r+.6806995451*g+.1073969566*b),s=Math.cbrt(.0883024619*r+.2817188376*g+.6299787005*b);
 const L=.2104542553*l+.793617785*m-.0040720468*s;
 const a=1.9779984951*l-2.428592205*m+.4505937099*s,b2=.0259040371*l+.7827717662*m-.808675766*s;
 return {L,C:Math.hypot(a,b2),h:(Math.atan2(b2,a)*180/Math.PI+360)%360};
}
const baseColor=hexToOklch('#cf6b32');
export const DEFAULTS=Object.freeze({opening:102,handScale:1.08,handStroke:10*STROKE_GAIN,starRatio:.46,ringGap:16,ringStroke:12*STROKE_GAIN,lightness:baseColor.L,chroma:baseColor.C,hue:baseColor.h});
export function validateParams(partial={}){
 for(const key of Object.keys(partial))if(!(key in SCHEMA))throw new TypeError(`Unknown logo parameter: ${key}`);
 const params={...DEFAULTS,...partial};
 for(const [key,spec] of Object.entries(SCHEMA)){
   const value=params[key];
   if(typeof value!=='number'||!Number.isFinite(value)||value<spec.min||value>spec.max)throw new RangeError(`${key} must be ${spec.min}..${spec.max}`);
 }
 return params;
}
function linearRgb(L,C,h){
 const radians=h*Math.PI/180,a=C*Math.cos(radians),b=C*Math.sin(radians);
 const l=(L+.3963377774*a+.2158037573*b)**3,m=(L-.1055613458*a-.0638541728*b)**3,s=(L-.0894841775*a-1.291485548*b)**3;
 return [4.0767416621*l-3.3077115913*m+.2309699292*s,-1.2684380046*l+2.6097574011*m-.3413193965*s,-.0041960863*l-.7034186147*m+1.707614701*s];
}
export function colorFromOklch(L,C,h){
 let appliedC=C,rgb=linearRgb(L,C,h);
 const inGamut=values=>values.every(x=>x>=-1e-7&&x<=1+1e-7);
 if(!inGamut(rgb)){
   let low=0,high=C;
   for(let i=0;i<28;i++){const mid=(low+high)/2;if(inGamut(linearRgb(L,mid,h)))low=mid;else high=mid;}
   appliedC=low;rgb=linearRgb(L,appliedC,h);
 }
 const bytes=rgb.map(x=>{x=Math.min(1,Math.max(0,x));return Math.round(255*(x<=.0031308?12.92*x:1.055*x**(1/2.4)-.055));});
 return {hex:'#'+bytes.map(x=>x.toString(16).padStart(2,'0')).join(''),L,h,desiredC:C,appliedC};
}
const add=(a,b)=>[a[0]+b[0],a[1]+b[1]],mul=(a,k)=>[a[0]*k,a[1]*k];
const mirror=p=>[-p[0],p[1]];
// Calibrated CSS-pixel targets, rather than unrelated SVG-unit size presets.
// The sublinear hand law adds relative weight at small sizes. The ring has
// a separate minimum-size allowance; neither coefficient is a universal law.
export function opticalTargets(size){
 if(typeof size!=='number'||!Number.isFinite(size)||size<24)throw new TypeError('Optical size must be at least 24px');
 return {hand:STROKE_GAIN*7.8*(size/200)**.81,ring:STROKE_GAIN*(1.7+.0383*size)};
}
function opticalProfile(profile){
 const size=profile==='large'?200:profile==='small'?32:profile;
 const target=opticalTargets(size);
 return {size,target,master:profile==='large',cuff:.75+.25/(1+(48/size)**4),sleeve:12+4/(1+(size/48)**4)};
}
function handSegments(params,sleeveDepth=12){
 const angle=params.opening*Math.PI/360,d=[-Math.sin(angle),Math.cos(angle)],n=[Math.cos(angle),Math.sin(angle)];
 const neck=[-18,25],length=42,half=11;
 const outer=add(neck,mul(n,-half)),inner=add(neck,mul(n,half));
 const tail=add(neck,mul(d,length)),tailOuter=add(tail,mul(n,-half)),tailInner=add(tail,mul(n,half));
 const cuff=add(neck,mul(d,length-sleeveDepth));
 // A shallow continuous bow gives the palm body without the earlier swollen back.
 return {segments:[['M',outer],['C',[-22,8],[-22,0],[-21,-8]],['C',[-20,-23],[-17,-40],[-14,-51]],['C',[-13.5,-58],[-9,-61],[-5,-59]],['C',[-1,-58],[0,-54],[0,-49]],['L',[0,8]],['C',[0,21],[-4,30],inner],['L',tailInner],['L',tailOuter],['Z']],cuff:[add(cuff,mul(n,-half)),add(cuff,mul(n,half))]};
}
function globalPoint(point,params,flipped=false){
 if(flipped)point=mirror(point);
 return [132+point[0]*params.handScale,134+point[1]*params.handScale];
}
function pathData(segments,params,flipped=false){
 return segments.map(([command,...points])=>command+points.map(point=>globalPoint(point,params,flipped).join(' ')).join(' ')).join('');
}
function bounds(points){const xs=points.map(p=>p[0]),ys=points.map(p=>p[1]);return {minX:Math.min(...xs),maxX:Math.max(...xs),minY:Math.min(...ys),maxY:Math.max(...ys)};}
function cubicPoint(points,t){
 const u=1-t;
 return [0,1].map(axis=>u**3*points[0][axis]+3*u*u*t*points[1][axis]+3*u*t*t*points[2][axis]+t**3*points[3][axis]);
}
function extremaTimes(values){
 const [p0,p1,p2,p3]=values,a=-p0+3*p1-3*p2+p3,b=2*(p0-2*p1+p2),c=p1-p0;
 if(Math.abs(a)<1e-12)return Math.abs(b)<1e-12?[]:[-c/b].filter(t=>t>0&&t<1);
 const discriminant=b*b-4*a*c;
 if(discriminant<0)return [];
 const root=Math.sqrt(discriminant);
 return [(-b-root)/(2*a),(-b+root)/(2*a)].filter(t=>t>0&&t<1);
}
function curvePoints(segments,params,flipped=false){
 let current;const result=[];
 for(const [command,...localPoints]of segments){
  const points=localPoints.map(p=>globalPoint(p,params,flipped));
  if(command==='M'||command==='L'){current=points[0];result.push(current);}
  else if(command==='C'){
   const cubic=[current,...points],times=[0,1,...extremaTimes(cubic.map(p=>p[0])),...extremaTimes(cubic.map(p=>p[1]))];
   times.forEach(t=>result.push(cubicPoint(cubic,t)));current=points[2];
  }
 }
 return result;
}
export function buildGeometry(partial={},profile='large'){
 const optical=opticalProfile(profile);
 const source=validateParams(partial);
 if(optical.master)return resolvedGeometry(source,{...optical,margin:8,sleeve:12,cuff:1});
 const target={hand:optical.target.hand*source.handStroke/DEFAULTS.handStroke,ring:optical.target.ring*source.ringStroke/DEFAULTS.ringStroke};
 const gap=Math.max(.75,source.ringGap*optical.size/256.75-.9*Math.exp(-((optical.size/48)**2)));
 const border=Math.max(.5,8*optical.size/256.75-.4*Math.exp(-optical.size/48));
 let canvas=256.75,result;
 // Frame extent depends on stroke edges. Solve that dependency so the emitted
 // strokes actually have the requested pixel widths after SVG scaling.
 for(let i=0;i<80;i++){
  const units=canvas/optical.size;
  const params={...source,handStroke:target.hand*units,ringStroke:target.ring*units,ringGap:gap*units};
  result=resolvedGeometry(params,{...optical,margin:border*units});
  const next=result.metrics.canvasSize;
  if(Math.abs(next-canvas)<1e-8)return result;
  canvas=next;
 }
 throw new RangeError('Optical frame did not converge');
}
function resolvedGeometry(params,optical){
 const shape=handSegments(params,optical.sleeve);
 const controls=shape.segments.flatMap(s=>s.slice(1)).concat(shape.cuff);
 const globalControls=controls.flatMap(p=>[globalPoint(p,params),globalPoint(p,params,true)]);
 // A Bezier stays in its control-point convex hull. Round joins/caps extend by w/2.
 const handRadiusBound=Math.max(...globalControls.map(([x,y])=>Math.hypot(x-128,y-128)));
 const radius=handRadiusBound+params.handStroke/2+params.ringStroke/2+params.ringGap+.01;
 const samples=curvePoints(shape.segments,params).concat(curvePoints(shape.segments,params,true));
 const handBounds=bounds(samples),handHeight=handBounds.maxY-handBounds.minY+params.handStroke;
 const starDiameter=handHeight*params.starRatio,starRadius=starDiameter/2;
 // The sparkle belongs to the hands; circle clearance no longer pushes it away.
 const anchor={x:132-50*params.handScale,y:handBounds.minY+3};
 const ringEnd={x:128-radius*Math.sqrt(3)/2,y:128-radius/2};
 const finish=[[ringEnd.x,ringEnd.y],[ringEnd.x+10,ringEnd.y-10*Math.sqrt(3)],[anchor.x-16,anchor.y],[anchor.x,anchor.y]];
 let progressLength=radius*Math.PI*5/3,previous=finish[0];
 for(let i=1;i<=64;i++){const point=cubicPoint(finish,i/64);progressLength+=Math.hypot(point[0]-previous[0],point[1]-previous[1]);previous=point;}
 const handColor=colorFromOklch(params.lightness,params.chroma,params.hue);
 const starColor=colorFromOklch(Math.min(.84,params.lightness+.08),params.chroma+.012,params.hue+8);
 // Leave space for the star's 1.55x pop as well as the full round progress cap.
 const margin=optical.margin;
 const extent=Math.max(128,radius+params.ringStroke/2+margin,Math.abs(128-anchor.x)+starRadius*1.55+margin,Math.abs(128-anchor.y)+starRadius*1.55+margin);
 const viewBox=[128-extent,128-extent,extent*2,extent*2];
 return {params,profile:optical.size<=48?'small':'large',opticalSize:optical.size,cuffFactor:optical.cuff,shape,handBounds,handHeight,handRadiusBound,radius,anchor,ringEnd,finish,progressLength,starDiameter,viewBox,colors:{hands:handColor,spark:starColor},metrics:{clearanceLowerBound:params.ringGap,handToRing:handHeight/(2*radius),starToHand:params.starRatio,canvasSize:extent*2,handStrokePx:params.handStroke*optical.size/(extent*2),ringStrokePx:params.ringStroke*optical.size/(extent*2),ringGapPx:params.ringGap*optical.size/(extent*2),handOccupancy:handHeight/(extent*2),gamutReduced:handColor.appliedC<handColor.desiredC-1e-5||starColor.appliedC<starColor.desiredC-1e-5}};
}
export function preset(name='balanced'){
 const params={...DEFAULTS};
 if(name==='golden'){
   params.starRatio=1/(PHI*PHI);
   const g=buildGeometry(params),wanted=g.handHeight*PHI/2-g.handRadiusBound-params.handStroke/2-params.ringStroke/2-.01;
   params.ringGap=Math.max(SCHEMA.ringGap.min,Math.min(SCHEMA.ringGap.max,wanted));
 }else if(name==='compact'){params.opening=96;params.ringGap=12;params.starRatio=.34;}
 else if(name!=='balanced')throw new TypeError('Unknown preset');
 return params;
}
function handsMarkup(g){
 const cuff=g.shape.cuff.map(p=>globalPoint(p,g.params));
 const cuffRight=g.shape.cuff.map(p=>globalPoint(p,g.params,true));
 const cuffWeight=g.params.handStroke*g.cuffFactor;
 return `<g fill="none" stroke="${g.colors.hands.hex}" stroke-width="${g.params.handStroke}" stroke-linecap="round" stroke-linejoin="round"><path d="${pathData(g.shape.segments,g.params)}"/><path d="${pathData(g.shape.segments,g.params,true)}"/><path class="wish-cuffs" stroke-width="${cuffWeight}" d="M${cuff[0].join(' ')}L${cuff[1].join(' ')}M${cuffRight[0].join(' ')}L${cuffRight[1].join(' ')}"/></g>`;
}
function sparkMarkup(g){
 const {x,y}=g.anchor,r=g.starDiameter/2,a=.095*r,b=.325*r;
 return `<path class="wish-spark" d="M${x} ${y-r}C${x+a} ${y-b} ${x+b} ${y-a} ${x+r} ${y}C${x+b} ${y+a} ${x+a} ${y+b} ${x} ${y+r}C${x-a} ${y+b} ${x-b} ${y+a} ${x-r} ${y}C${x-b} ${y-a} ${x-a} ${y-b} ${x} ${y-r}Z" fill="${g.colors.spark.hex}"/>`;
}
function progressMarkup(g){return `<path class="wish-progress" d="M128 ${128-g.radius}A${g.radius} ${g.radius} 0 1 1 ${g.ringEnd.x} ${g.ringEnd.y}C${g.finish.slice(1).map(p=>p.join(' ')).join(' ')}" pathLength="100" fill="none" stroke="${g.colors.hands.hex}" stroke-width="${g.params.ringStroke}" stroke-linecap="round" stroke-dasharray="100" stroke-dashoffset="100"/>`;}
export const completeKeyframes=`@keyframes wish-pop{0%{opacity:0;transform:scale(0) rotate(-45deg)}12%{opacity:1;transform:scale(.35) rotate(0deg)}45%{opacity:1;transform:scale(1.55) rotate(170deg)}75%{opacity:1;transform:scale(.82) rotate(320deg)}100%{opacity:1;transform:scale(1) rotate(360deg)}}`;
function animationStyle(g){return `<style>.wish-progress{animation:wish-draw 3.8s linear both}.wish-spark{transform-box:view-box;transform-origin:${g.anchor.x}px ${g.anchor.y}px;animation:wish-demo-pop 3.8s linear both}@keyframes wish-draw{0%{stroke-dashoffset:100;opacity:0}.01%{opacity:1}66%{stroke-dashoffset:0;opacity:1}66.01%,100%{stroke-dashoffset:0;opacity:0}}@keyframes wish-demo-pop{0%,66%{opacity:0;transform:scale(0) rotate(-45deg)}70%{opacity:1;transform:scale(.35) rotate(0deg)}80%{opacity:1;transform:scale(1.55) rotate(170deg)}91%{opacity:1;transform:scale(.82) rotate(320deg)}100%{opacity:1;transform:scale(1) rotate(360deg)}}@media(prefers-reduced-motion:reduce){.wish-progress{animation:none;opacity:0}.wish-spark{animation:none;opacity:1;transform:none}}</style>`;}
export function svgFor(params={},mode='static',grid=false,profile='large'){
 if(!['static','animated','controlled'].includes(mode))throw new TypeError('Unknown SVG mode');
 const g=buildGeometry(params,profile),controlled=mode==='controlled';
 let guides='';
 if(grid){
   const [start,,span]=g.viewBox;const first=Math.ceil(start/8)*8,last=start+span;
   for(let v=first;v<=last;v+=8)guides+=`<path d="M${v} ${start}V${last}M${start} ${v}H${last}"/>`;
   guides=`<g stroke="#d9dacf" stroke-width=".5" opacity=".65" fill="none">${guides}</g><g stroke="#aa9a7c" stroke-width="1" fill="none"><path d="M128 ${start}V${last}M${start} 128H${last}"/><rect x="${g.handBounds.minX-g.params.handStroke/2}" y="${g.handBounds.minY-g.params.handStroke/2}" width="${g.handBounds.maxX-g.handBounds.minX+g.params.handStroke}" height="${g.handHeight}" stroke-dasharray="3 4"/></g>`;
 }
 return `<svg xmlns="http://www.w3.org/2000/svg" ${controlled?'class="wish-loader" aria-hidden="true"':'role="img" aria-label="WishOS folded-hands logo"'} data-optical-size="${g.opticalSize}" viewBox="${g.viewBox.join(' ')}" width="160" height="160" style="--wish-spark-x:${g.anchor.x}px;--wish-spark-y:${g.anchor.y}px">${mode==='animated'?animationStyle(g):''}${guides}${mode==='static'?'':progressMarkup(g)}${handsMarkup(g)}${sparkMarkup(g)}</svg>\n`;
}
