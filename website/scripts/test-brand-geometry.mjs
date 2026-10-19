import assert from 'node:assert/strict';
import {buildGeometry,DEFAULTS,SCHEMA,PHI,preset,colorFromOklch,hexToOklch,svgFor,validateParams,opticalTargets} from './brand-geometry.mjs';
for(const color of ['#cf6b32','#ee8d2a','#ffffff','#000000','#ff0000']){
 const c=hexToOklch(color);assert.equal(colorFromOklch(c.L,c.C,c.h).hex,color,'Color conversion roundtrip');
}
const initial=buildGeometry(DEFAULTS);
const small=buildGeometry(DEFAULTS,'small');
assert.ok(small.params.ringStroke/small.metrics.canvasSize>initial.params.ringStroke/initial.metrics.canvasSize,'Small progress has a heavier visual stroke');
assert.ok(small.metrics.clearanceLowerBound<initial.metrics.clearanceLowerBound,'Small ring uses compact spacing');
const at24=buildGeometry(DEFAULTS,24),at48=buildGeometry(DEFAULTS,48),at96=buildGeometry(DEFAULTS,96);
assert.ok(24*at24.params.ringStroke/at24.metrics.canvasSize>=2,'24px progress remains at least two pixels thick');
assert.ok(at24.params.ringStroke/at24.params.handStroke>=1.8,'24px progress ring remains substantially heavier than the hand outline');
assert.ok(at48.params.ringStroke/at48.metrics.canvasSize<at24.params.ringStroke/at24.metrics.canvasSize,'48px ring relaxes its relative weight rather than doubling 24px strokes');
assert.ok(at96.params.handStroke/at96.metrics.canvasSize>initial.params.handStroke/initial.metrics.canvasSize,'96px hands are heavier than a scaled large mark');
assert.throws(()=>buildGeometry({},'unknown'),TypeError);
let previous;
for(let size=24;size<=320;size++){
 const g=buildGeometry(DEFAULTS,size),target=opticalTargets(size),m=g.metrics;
 assert.ok(Math.abs(m.handStrokePx-target.hand)<1e-7&&Math.abs(m.ringStrokePx-target.ring)<1e-7,'SVG scaling reproduces requested CSS-pixel stroke widths');
 if(previous){
  assert.ok(m.handStrokePx>previous.hand&&m.ringStrokePx>previous.ring,'Both stroke widths grow continuously with size');
  assert.ok(m.handStrokePx/size<previous.handRelative&&m.ringStrokePx/size<previous.ringRelative,'Relative stroke weights decrease at larger optical sizes');
 }
 assert.equal(g.params.handScale,DEFAULTS.handScale,'Contour scale is not independently changed by optical presets');
 assert.ok(m.handOccupancy>.5&&m.handOccupancy<.56,'Foreground stays within a narrow measured framing band');
 previous={hand:m.handStrokePx,ring:m.ringStrokePx,handRelative:m.handStrokePx/size,ringRelative:m.ringStrokePx/size};
}
assert.throws(()=>validateParams({opening:NaN}),RangeError);
assert.throws(()=>validateParams({ringGap:0}),RangeError);
assert.throws(()=>validateParams({nativePriority:100}),TypeError);
assert.equal(initial.params.opening,DEFAULTS.opening,'Invalid proposals do not mutate defaults');
const golden=buildGeometry(preset('golden'));
assert.ok(Math.abs(golden.metrics.handToRing-1/PHI)<1e-9,'Golden preset matches its stated size ratio');
const gamut=colorFromOklch(.65,.5,55);
assert.ok(gamut.appliedC<gamut.desiredC,'Out-of-gamut colors explicitly retain desired and applied chroma');
for(const opening of [SCHEMA.opening.min,DEFAULTS.opening,SCHEMA.opening.max]){
 for(const handScale of [SCHEMA.handScale.min,DEFAULTS.handScale,SCHEMA.handScale.max]){
  for(const ringGap of [SCHEMA.ringGap.min,DEFAULTS.ringGap,SCHEMA.ringGap.max]){
   for(const ringStroke of [SCHEMA.ringStroke.min,SCHEMA.ringStroke.max]){
    const input={opening,handScale,ringGap,ringStroke,handStroke:SCHEMA.handStroke.max,starRatio:SCHEMA.starRatio.max};
    for(const profile of ['large',24,48,96]){
    const g=buildGeometry(input,profile);
    const actualBound=g.radius-g.params.ringStroke/2-(g.handRadiusBound+g.params.handStroke/2);
    assert.ok(actualBound>=g.params.ringGap,'Stroke edges preserve required minimum ring clearance');
    const [x,y,w,h]=g.viewBox,extent=g.radius+g.params.ringStroke/2;
    assert.ok(128-extent>=x&&128+extent<=x+w&&128-extent>=y&&128+extent<=y+h,'Full progress ring stays inside the canvas');
    const starExtent=g.starDiameter/2*1.55;
    assert.ok(g.anchor.x-starExtent>=x&&g.anchor.y-starExtent>=y,'Sparkle pop is not clipped');
    const mark=svgFor(input,'controlled',false,profile);
    assert.ok(!mark.includes('wish-track'),'No background progress track');
    assert.ok(!mark.includes('NaN'),'Extreme combinations produce valid numeric geometry');
    }
   }
  }
 }
}
console.log('Verified 216 geometry boundaries and 297 optical sizes: monotonic stroke laws, final pixel widths, stable framing, clearance, pop bounds, color/gamut mapping and golden reference. Aesthetic preference remains a visual review.');
