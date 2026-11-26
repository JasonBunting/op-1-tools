/* Shared vector geometry: no browser APIs, fonts, dependencies or firmware code. */
(function (root) {
  'use strict';
  const C = { bg: '#202020', blue: '#6584ff', green: '#00d99b', white: '#dcd5ff', red: '#ff355e', dim: '#493357' };
  const SQ = Math.SQRT1_2, ORIGIN = [140, 73], MIC_DIR = [SQ, SQ];
  const f = n => Number(n.toFixed(3));
  const fmt = p => `${f(p[0])},${f(p[1])}`;
  const cross = (a,b) => [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
  const sub = (a,b) => a.map((v,i)=>v-b[i]);
  const dot = (a,b) => a.reduce((s,v,i)=>s+v*b[i],0);
  function rotate(p, a) { return [p[0]*Math.cos(a)-p[1]*Math.sin(a),p[0]*Math.sin(a)+p[1]*Math.cos(a),p[2]]; }
  function project(p) { return [ORIGIN[0]+p[0],ORIGIN[1]+SQ*(p[1]-p[2])]; }
  function path(points, close=false) { return `M${points.map(fmt).join('L')}${close?'Z':''}`; }
  function stroke(d,color,width=1.35,fill='none') { return `<path d="${d}" fill="${fill}" stroke="${color}" stroke-width="${width}" stroke-linejoin="round" stroke-linecap="round"/>`; }
  function ring(x,w,h,r) {
    const pts=[];
    for (const [cy,cz,start] of [[w-r,h-r,0],[-w+r,h-r,90],[-w+r,-h+r,180],[w-r,-h+r,270]]) {
      for (let j=0;j<5;j++) { const t=(start+j*22.5)*Math.PI/180; pts.push([x,cy+r*Math.cos(t),16+cz+r*Math.sin(t)]); }
    }
    return pts;
  }
  const sections = [[0,3.3,3.6,1.6],[4,4.3,3.8,1.8],[9,6.2,4.3,2.1],[15,9.2,5.2,2.6],[22,13,6.5,3.1]];
  const rings=sections.map(s=>ring(...s).map(p=>p.map(v=>v*1.35))), N=rings[0].length;
  const vertices=rings.flat();
  const radius=Math.max(...vertices.map(v=>Math.hypot(v[0],v[1])));
  const faces=[];
  for(let r=0;r<rings.length-1;r++) for(let j=0;j<N;j++) {
    const ids=[r*N+j,r*N+(j+1)%N,(r+1)*N+(j+1)%N,(r+1)*N+j];
    let normal=cross(sub(vertices[ids[1]],vertices[ids[0]]),sub(vertices[ids[3]],vertices[ids[0]]));
    const mid=ids.map(i=>vertices[i]).reduce((a,p)=>a.map((v,k)=>v+p[k]/4),[0,0,0]);
    if(dot(normal,[0,mid[1],mid[2]-21.6])<0) normal=normal.map(v=>-v);
    faces.push({ids,normal});
  }
  faces.push({ids:Array.from({length:N},(_,j)=>j),normal:[-1,0,0],cap:'back'});
  faces.push({ids:Array.from({length:N},(_,j)=>(rings.length-1)*N+j),normal:[1,0,0],cap:'mouth'});
  const edges=new Map();
  faces.forEach((face,fi)=>face.ids.forEach((a,j)=>{const b=face.ids[(j+1)%face.ids.length], key=[a,b].sort((x,y)=>x-y).join(':'); if(!edges.has(key))edges.set(key,{a,b,faces:[]});edges.get(key).faces.push(fi);}));
  function horn(angle) {
    const world=vertices.map(p=>rotate(p,angle)), pts=world.map(project);
    const visible=faces.map(face=>dot(rotate(face.normal,angle),[0,SQ,SQ])>1e-7);
    const order=faces.map((face,i)=>({face,i,depth:face.ids.reduce((s,j)=>s+SQ*(world[j][1]+world[j][2])/face.ids.length,0)})).filter(o=>visible[o.i]).sort((a,b)=>a.depth-b.depth);
    let out=order.map(o=>`<path d="${path(o.face.ids.map(j=>pts[j]),true)}" fill="${C.bg}" stroke="${C.bg}" stroke-width="0.3"/>`).join('');
    // Silhouette edges plus the visible mouth/back rims, never a transparent wireframe.
    let outline='';
    for(const edge of edges.values()) {
      const seen=edge.faces.filter(i=>visible[i]);
      const rim=seen.some(i=>faces[i].cap);
      if(seen.length===1 || rim) outline+=`M${fmt(pts[edge.a])}L${fmt(pts[edge.b])}`;
    }
    out+=stroke(outline,C.white,1.45);
    return out;
  }
  function pivot() {
    const bottom=project([0,0,0]),top=project([0,0,33.75]);
    return `<ellipse cx="${bottom[0]}" cy="${bottom[1]}" rx="9" ry="4.8" fill="none" stroke="${C.blue}" stroke-width="1.3"/>`+
      stroke(`M${top[0]-3},${top[1]}V${bottom[1]-1}Q${bottom[0]},${bottom[1]+2} ${bottom[0]+3},${bottom[1]-1}V${top[1]}`,C.blue,1.3,C.bg)+
      `<ellipse cx="${top[0]}" cy="${top[1]}" rx="3" ry="1.7" fill="${C.bg}" stroke="${C.blue}" stroke-width="1.3"/>`;
  }
  function pivotTip() {
    const a=project([0,0,33.75]),b=project([0,0,28.35]);
    return stroke(`M${a[0]-3},${a[1]}V${b[1]}M${a[0]+3},${a[1]}V${b[1]}`,C.blue,1.3)+`<ellipse cx="${a[0]}" cy="${a[1]}" rx="3" ry="1.7" fill="${C.bg}" stroke="${C.blue}" stroke-width="1.3"/>`;
  }
  function microphone(proximity) {
    const distance=96+Math.max(0,Math.min(100,proximity))*0.32;
    const base=project([distance*MIC_DIR[0],distance*MIC_DIR[1],0]);
    const head=project([distance*MIC_DIR[0],distance*MIC_DIR[1],18]);
    const angle=180/Math.PI*Math.atan2(SQ*MIC_DIR[1],MIC_DIR[0]);
    return `<g data-mic-distance="${f(distance)}">`+
      `<ellipse cx="${f(base[0])}" cy="${f(base[1])}" rx="5.6" ry="2.7" stroke="${C.green}" stroke-width="1.35" fill="none"/>`+
      stroke(`M${fmt(base)}L${fmt(head)}`,C.green,1.35)+
      `<g transform="translate(${fmt(head)}) rotate(${f(angle)})"><rect x="-11" y="-5" width="22" height="10" rx="5" fill="${C.bg}" stroke="${C.green}" stroke-width="1.4"/>`+
      stroke('M-3,-2V2M1,-2V2M5,-2V2',C.green,1.25)+`</g></g>`;
  }
  // Single-stroke lettering keeps the corner treatment independent of installed fonts.
  const glyph={
    A:'M0 12L3.5 0 7 12M1.2 8H5.8',B:'M0 12V0H3.5Q7 0 7 3T3.5 6H0M3.5 6Q7 6 7 9T3.5 12H0',
    C:'M7 1Q5.5 0 3.5 0Q0 0 0 6T3.5 12Q5.5 12 7 11',D:'M0 12V0H2.5Q7 0 7 6T2.5 12Z',E:'M7 0H0V12H7M0 6H6',F:'M7 0H0V12M0 6H6',
    I:'M3.5 0V12',M:'M0 12V0L3.5 7 7 0V12',N:'M0 12V0L7 12V0',
    P:'M0 12V0H3.5Q7 0 7 3.2T3.5 6.4H0',R:'M0 12V0H3.5Q7 0 7 3T3.5 6H0M3.5 6L7 12',
    S:'M7 1Q5.5 0 3.5 0Q0 0 0 3Q0 5 3.5 6Q7 7 7 9Q7 12 3.5 12Q1 12 0 11',
    L:'M0 0V12H7',O:'M3.5 0Q0 0 0 6T3.5 12Q7 12 7 6T3.5 0Z',
    W:'M0 0L1.5 12 3.5 5 5.5 12 7 0',V:'M0 0L3.5 12 7 0',K:'M0 0V12M7 0L0 6 7 12',
    T:'M0 0H7M3.5 0V12',X:'M0 0L7 12M7 0L0 12',
    '0':'M3.5 0Q0 0 0 6T3.5 12Q7 12 7 6T3.5 0Z',
    '1':'M0 3L3.5 0V12M0 12H7',
    '2':'M0 2Q0 0 3.5 0Q7 0 7 3Q7 4.5 5 7L0 12H7',
    '3':'M0 1Q7 -2 7 3Q7 6 3.5 6Q7 6 7 9Q7 14 0 11',
    '4':'M6 12V0L0 8H7','5':'M7 0H0V6Q7 3 7 9Q7 13 0 11',
    '6':'M7 1Q0 -3 0 7V9Q0 12 3.5 12Q7 12 7 9Q7 5 0 7',
    '7':'M0 0H7L2 12','8':'M3.5 6Q0 6 0 3T3.5 0Q7 0 7 3T3.5 6Q0 6 0 9T3.5 12Q7 12 7 9T3.5 6',
    '9':'M0 11Q7 15 7 5V3Q7 0 3.5 0Q0 0 0 3Q0 7 7 5'
  };
  function text(value,x,y,height,color,width=.7) {
    const scale=height/12;
    return `<g transform="translate(${x},${y}) scale(${scale})" stroke="${color}" stroke-width="${width}" fill="none" stroke-linecap="butt" stroke-linejoin="round" aria-label="${value}">`+
      [...value].map((c,i)=>`<path d="${glyph[c]||''}" transform="translate(${i*9},0)"/>`).join('')+'</g>';
  }
  function corners(state) {
    const mic=state.proximity<34?'NEAR':state.proximity>66?'FAR':'MID';
    return text('SPEED',19,16,13,C.blue)+text(state.mode.toUpperCase(),19,38,13,C.blue)+
      text('MIC',269,16,13,C.green)+text(mic,262,38,13,C.green)+
      text('DRIVE',19,80,13,C.white)+text(String(state.drive),19,102,13,C.white)+
      text('MIX',263,80,13,C.red)+Array.from({length:8},(_,i)=>`<path d="M279 ${101+i*6}H296" stroke="${i>=8-Math.round(state.mix*8/100)?C.red:C.dim}" stroke-width="1.45"/>`).join('');
  }
  function scene(state) {
    return '<rect width="320" height="160" fill="'+C.bg+'"/>'+corners(state)+
      `<g id="rotary-mechanism">${pivot()}${horn(state.angle)}${pivotTip()}${microphone(state.proximity)}</g>`;
  }
  function svg(state) { return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 320 160" width="320" height="160" role="img" aria-label="Rotary effect with a rotating horn and a microphone arranged diagonally"><title>Rotary effect screen</title>${scene(state)}</svg>`; }
  const api={C,ORIGIN,vertices,faces,radius,project,rotate,horn,pivot,pivotTip,microphone,corners,scene,svg,
    defaults:{angle:1.05,proximity:20,drive:12,mix:100,mode:'slow'},
    // Conservative bound: invert the 45-degree projection of the entire mic capsule.
    micMinimumRadius:96-18};
  if(typeof module==='object' && module.exports)module.exports=api;
  else root.Rotor147Geometry=api;
})(globalThis);
