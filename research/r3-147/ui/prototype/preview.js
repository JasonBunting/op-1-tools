(function(){
  'use strict';
  const root=document.getElementById('rotary147-preview');
  const G=globalThis.Rotor147Geometry, screen=root.querySelector('.rotary-screen');
  const state={...G.defaults}, control=name=>root.querySelector(`[data-control="${name}"]`);
  const value=name=>root.querySelector(`[data-value="${name}"]`);
  const play=root.querySelector('[data-action="play"]'),status=root.querySelector('[data-status]');
  let running=false, speed=.8, last=0, raf=0, savedSignature='';
  const reduced=matchMedia('(prefers-reduced-motion: reduce)');
  function render(){
    screen.innerHTML='<title>Rotary effect</title>'+G.scene(state);
    const degrees=Math.round(state.angle*180/Math.PI)%360;
    control('angle').value=degrees; value('angle').textContent=degrees+'°';
    for(const key of ['proximity','drive','mix']) {control(key).value=state[key];value(key).textContent=state[key]+(key==='drive'?'':'%');}
    control('mode').value=state.mode;
  }
  function updateStatus(){
    play.textContent=running?'Pause rotation':'Play rotation';play.setAttribute('aria-pressed',String(running));
    status.textContent=(running?(state.mode==='brake'?'Coasting to brake':'Rotating'): 'Paused')+' · visual preview only';
  }
  function save(){
    const snapshot={modelContent:{design:'Short scoop, diagonal arrangement; no guide dashes',mode:state.mode,proximity:state.proximity,drive:state.drive,mix:state.mix},privateContent:{angle:state.angle,actual:control('actual').checked}};
    savedSignature=JSON.stringify(snapshot);
    if(window.openai?.setWidgetState)window.openai.setWidgetState(snapshot).catch(()=>{});
  }
  function restore(snapshot){
    const m=snapshot?.modelContent||{}, p=snapshot?.privateContent||{};
    if(['slow','fast','brake'].includes(m.mode))state.mode=m.mode;
    for(const key of ['proximity','drive','mix'])if(Number.isFinite(m[key]))state[key]=Math.max(0,Math.min(key==='drive'?99:100,m[key]));
    if(Number.isFinite(p.angle))state.angle=((p.angle%(Math.PI*2))+Math.PI*2)%(Math.PI*2);
    control('actual').checked=!!p.actual;
    root.querySelector('.rotary-stage').classList.toggle('actual',!!p.actual);
    speed=state.mode==='fast'?6.7:state.mode==='slow'?.8:0;
    render();updateStatus();
  }
  function step(now){
    if(!running)return;
    const dt=last?Math.min(.05,(now-last)/1000):0; last=now;
    const target=state.mode==='fast'?6.7:state.mode==='slow'?.8:0;
    const slew=target>speed?5.9:state.mode==='brake'?3.35:3.94;
    const change=Math.sign(target-speed)*Math.min(Math.abs(target-speed),slew*dt);
    state.angle=(state.angle+(speed+change/2)*dt*Math.PI*2)%(Math.PI*2);
    speed+=change;render();
    if(state.mode==='brake' && speed===0) {running=false;updateStatus();status.textContent='Stopped · visual preview only';save();return;}
    raf=requestAnimationFrame(step);
  }
  function pause(){running=false;cancelAnimationFrame(raf);last=0;updateStatus();}
  play.addEventListener('click',()=>{if(running)pause();else{running=true;last=0;updateStatus();raf=requestAnimationFrame(step);}save();});
  control('mode').addEventListener('change',()=>{state.mode=control('mode').value;render();updateStatus();save();});
  control('angle').addEventListener('input',()=>{pause();state.angle=Number(control('angle').value)*Math.PI/180;render();});
  control('angle').addEventListener('change',save);
  for(const key of ['proximity','drive','mix']) {
    control(key).addEventListener('input',()=>{state[key]=Number(control(key).value);render();});
    control(key).addEventListener('change',save);
  }
  control('actual').addEventListener('change',()=>{root.querySelector('.rotary-stage').classList.toggle('actual',control('actual').checked);save();});
  document.addEventListener('visibilitychange',()=>{last=0;if(document.hidden&&running)pause();});
  reduced.addEventListener('change',e=>{if(e.matches)pause();});
  window.addEventListener('openai:set_globals',e=>{
    const snapshot=e.detail?.globals?.widgetState;
    if(snapshot && JSON.stringify(snapshot)!==savedSignature){pause();restore(snapshot);}
  });
  restore(window.openai?.widgetState);
  // Explicit Play is required, including when reduced-motion is requested.
})();
