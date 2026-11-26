const fs=require('node:fs');
const path=require('node:path');
const G=require('./rotor-geometry.js');
const dir=path.dirname(__dirname);
const template=fs.readFileSync(path.join(__dirname,'preview-template.html'),'utf8');
const html=template.replace('<!-- INITIAL_SCREEN -->',()=>G.scene(G.defaults))
  .replace('/* GEOMETRY_SCRIPT */',()=>fs.readFileSync(path.join(__dirname,'rotor-geometry.js'),'utf8'))
  .replace('/* PREVIEW_SCRIPT */',()=>fs.readFileSync(path.join(__dirname,'preview.js'),'utf8'));
fs.writeFileSync(path.join(dir,'rotary-preview.html'),html);
fs.writeFileSync(path.join(dir,'rotary-screen.svg'),G.svg(G.defaults));
console.log('Built rotary-preview.html and rotary-screen.svg');
