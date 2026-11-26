const assert=require('node:assert/strict');
const G=require('./rotor-geometry.js');
let minimum=Infinity, minX=Infinity,maxX=-Infinity,minY=Infinity,maxY=-Infinity;
for(let degree=0;degree<360;degree++) {
  const angle=degree*Math.PI/180;
  for(const vertex of G.vertices) {
    const p=G.rotate(vertex,angle),q=G.project(p);
    assert(Math.abs(Math.hypot(p[0],p[1])-Math.hypot(vertex[0],vertex[1]))<1e-9);
    minimum=Math.min(minimum,G.micMinimumRadius-Math.hypot(p[0],p[1]));
    minX=Math.min(minX,q[0]);maxX=Math.max(maxX,q[0]);minY=Math.min(minY,q[1]);maxY=Math.max(maxY,q[1]);
  }
  assert(!/NaN|undefined|Infinity/.test(G.scene({...G.defaults,angle})));
}
assert(minimum>20,'Microphone must clear the whole rotation envelope, including its capsule');
assert(minX>90&&maxX<250&&minY>10&&maxY<150,'Horn must stay clear of the corner controls and screen edges');
assert.notEqual(G.horn(Math.PI/2),G.horn(Math.PI*1.5),'Front and rear must render differently');
assert.equal(G.corners(G.defaults),G.corners({...G.defaults,angle:2.3}),'Corner geometry must not change with horn angle');
assert(!G.scene(G.defaults).includes('stroke-dasharray'),'No proximity dashes');
console.log(JSON.stringify({anglesChecked:360,hornRadius:G.radius,minimumMicRadialClearance:minimum,projectedHornBounds:{minX,maxX,minY,maxY}},null,2));
