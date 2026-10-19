import assert from 'node:assert/strict';
import { createWishLoader } from '../public/loader.js';
class Element extends EventTarget {
  dataset = {};
  style = {};
  attributes = new Map();
  setAttribute(name,value) { this.attributes.set(name,value); }
  removeAttribute(name) { this.attributes.delete(name); }
  getAttribute(name) { return this.attributes.get(name); }
}
function fixture(reduced=false) {
  const progress = new Element();
  const spark = new Element();
  const root = new Element();
  root.querySelector = selector => selector === '.wish-progress' ? progress : spark;
  const motion = new EventTarget(); motion.matches = reduced;
  globalThis.matchMedia = () => motion;
  const updates=[];
  const loader = createWishLoader(root,{onState:state=>updates.push(state)});
  return {root,progress,spark,motion,updates,loader};
}
function finishAnimation(f) {
  const event = new Event('animationend');
  Object.defineProperty(event,'animationName',{value:'wish-pop'});
  f.spark.dispatchEvent(event);
}
{
  const f=fixture();
  f.loader.setProgress(100);
  assert.equal(f.loader.state,'loading','Progress alone cannot signal completion');
  assert.throws(()=>f.loader.setProgress(Number.NaN),RangeError);
  assert.throws(()=>f.loader.setProgress(101),RangeError);
  assert.equal(f.root.getAttribute('aria-valuenow'),'100','Rejected updates preserve prior state');
  assert.equal(f.loader.complete(),true);
  assert.equal(f.loader.state,'completing');
  finishAnimation(f);
  assert.equal(f.loader.state,'complete');
  assert.equal(f.loader.setProgress(20),false,'Late progress cannot reopen a completed task');
  f.loader.reset();
  f.loader.complete();
  f.loader.fail();
  finishAnimation(f);
  assert.equal(f.loader.state,'error','Late animation events cannot convert failure to success');
  assert.equal(f.loader.complete(),false,'Failure needs an explicit new task');
  f.loader.reset();
  assert.equal(f.loader.state,'loading');
  assert.equal(f.root.getAttribute('aria-valuenow'),'0');
  const count=f.updates.length;
  f.loader.destroy();
  f.loader.complete(); f.loader.reset(); finishAnimation(f);
  assert.equal(f.updates.length,count,'Destroyed loaders ignore future notifications');
}
{
  const f=fixture(true);
  f.loader.complete();
  assert.equal(f.loader.state,'complete','Reduced motion must not wait for an animation event');
}
{
  const f=fixture();
  f.loader.complete();
  f.motion.matches=true;
  f.motion.dispatchEvent(new Event('change'));
  assert.equal(f.loader.state,'complete','Live reduced-motion changes settle pending animations');
}
console.log('Verified loader progress/receipt separation, rejection, failure/reset, late events, cleanup and reduced motion. DOM fixtures do not replace browser visual checks.');
