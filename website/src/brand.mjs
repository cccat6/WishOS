// Production uses frozen assets. The generator and design study stay internal.
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
const assets=new URL('../assets/brand/',import.meta.url);
export const approvedBrand=JSON.parse(readFileSync(new URL('../assets/approved-brand.json',import.meta.url),'utf8'));
export const completeKeyframes=readFileSync(new URL('completion.css',assets),'utf8');
export function controlledMark(size=200){
 if(!approvedBrand.sizes.includes(size))throw new RangeError(`No approved loader at ${size}px`);
 const name=`controlled-${size}.svg`,svg=readFileSync(new URL(name,assets),'utf8');
 if(createHash('sha256').update(svg).digest('hex')!==approvedBrand.sha256[name])throw new Error(`Approved SVG changed: ${name}`);
 return svg;
}
export function assetPath(name){return new URL(name,assets);}
