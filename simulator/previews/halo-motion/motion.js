/* Procedural motion sketches. Browser renderers, not firmware performance claims. */
(() => {
  'use strict';
  const TAU = Math.PI * 2;
  const mix = (a,b,t) => a+(b-a)*t;
  const clamp = (v,a=0,b=1) => Math.max(a,Math.min(b,v));
  const vertex = `attribute vec2 position; void main(){gl_Position=vec4(position,0.,1.);}`;
  const fragment = `
precision highp float;
uniform vec2 resolution;
uniform float time,phase,energy,expansion,weave,cool,warm,kind;
mat2 rot(float a){float s=sin(a),c=cos(a);return mat2(c,-s,s,c);}
vec3 orient(vec3 p){p.xz=rot(.28+phase*.13)*p.xz;p.yz=rot(.32+.12*sin(phase*.19))*p.yz;return p;}
float field(vec3 p){
 p=orient(p);float breath=1.+.018*sin(time*1.15)+expansion*.1+energy*.12;
 p/=breath;
 if(kind<.5){
   float f=sin(p.x*2.8+phase*.41)*sin(p.y*3.1-phase*.29)*sin(p.z*2.7+phase*.25);
   float g=sin(p.y*4.+p.x*1.2+phase*.52)*cos(p.z*3.-phase*.3);
   return (length(p)-(.91+f*(.065+weave*.025)+g*.027))*breath;
 }
 p.xy=rot(-phase*.16)*p.xy;
 float a=atan(p.y,p.x),r=length(p.xy);
 float flower=.66+.075*cos(a*3.+phase*.15)+expansion*.045;
 vec2 q=vec2(r-flower,p.z-.16*sin(a*3.+phase*.23));
 q=rot(a*1.5+phase*.2)*q;
 vec2 section=vec2(.205+energy*.055,.066+weave*.014);
 return (length(q/section)-1.)*min(section.x,section.y)*breath;
}
vec3 normalAt(vec3 p){vec2 e=vec2(.002,0.);return normalize(vec3(field(p+e.xyy)-field(p-e.xyy),field(p+e.yxy)-field(p-e.yxy),field(p+e.yyx)-field(p-e.yyx)));}
vec3 colorAt(vec3 p,vec3 n,vec3 ray){
 vec3 q=orient(p);
 float flow=sin(q.x*2.3+q.y*2.1+q.z*2.7+phase*.25)*.5+.5;
 float ribbons=sin(q.x*3.1-q.y*2.4+sin(q.z*3.+phase*.4)+phase*.22)*.5+.5;
 vec3 lavender=vec3(.48,.29,.85),mint=vec3(.19,.82,.77),pearl=vec3(.86,.84,1.);
 vec3 base=mix(lavender,mint,smoothstep(.15,.9,flow+cool*.13));
 base=mix(base,pearl,.18+pow(ribbons,4.)*.62);
 base=mix(base,vec3(.96,.46,.31),warm*.72);
 vec3 light=normalize(vec3(-.55,.8,1.1)),other=normalize(vec3(.8,-.4,.7));
 float diffuse=max(0.,dot(n,light)),fill=max(0.,dot(n,other));
 float facing=max(0.,dot(n,-ray)),rim=pow(1.-facing,2.6);
 vec3 reflected=reflect(ray,n);
 float area=exp(-pow((reflected.x+reflected.y*.45+.22)*7.,2.))*smoothstep(-.15,.8,reflected.y);
 float spec=pow(max(0.,dot(reflect(-light,n),-ray)),65.);
 float soft=pow(max(0.,dot(reflect(-other,n),-ray)),18.);
 vec3 color=base*(.12+diffuse*.68+fill*.23);
 color+=vec3(.78,.93,1.)*(area*.72+spec*.7);
 color+=vec3(.65,.46,.95)*soft*.37;
 color+=mix(vec3(.47,.4,.94),vec3(.5,.96,.84),flow)*rim*.82;
 float silk=exp(-pow((ribbons-.48)*11.,2.));
 color+=base*silk*(.09+energy*.12);
 float caustic=exp(-pow(sin(q.x*3.6+q.y*1.6+sin(q.z*3.+phase*.25))*6.,2.));
 if(kind<.5)color+=mix(mint,pearl,.72)*caustic*(.09+.08*facing);
 if(kind>.5)color+=base*.07+pearl*rim*.18;
 color*=.88+energy*.13;
 return pow(max(color,vec3(0.)),vec3(.87));
}
void main(){
 vec2 uv=(gl_FragCoord.xy-.5*resolution)/min(resolution.x,resolution.y)*3.25;uv.y-=.08;
 vec3 origin=vec3(0.,0.,4.2),ray=normalize(vec3(uv,-4.5));
 float travel=2.65;bool hit=false;
 for(int i=0;i<88;i++){vec3 p=origin+ray*travel;float distance=field(p);if(distance<.0014){hit=true;break;}travel+=max(.001,distance*.78);if(travel>6.)break;}
 vec3 color=vec3(0.);
 float halo=exp(-dot(uv,uv)*1.9);
 color+=vec3(.025,.014,.05)*halo;
 if(hit){vec3 p=origin+ray*travel;vec3 n=normalAt(p);color=colorAt(p,n,ray);}
 gl_FragColor=vec4(clamp(color,0.,1.),1.);
}`;
  function shader(gl,type,source){
    const s=gl.createShader(type);gl.shaderSource(s,source);gl.compileShader(s);
    if(!gl.getShaderParameter(s,gl.COMPILE_STATUS))throw Error(gl.getShaderInfoLog(s));return s;
  }
  class Sculpture {
    constructor(canvas,kind){
      this.canvas=canvas;this.kind=kind;
      const gl=this.gl=canvas.getContext('webgl',{alpha:false,antialias:false,preserveDrawingBuffer:true,powerPreference:'low-power'});
      if(!gl)throw Error('WebGL unavailable');
      const program=this.program=gl.createProgram();
      gl.attachShader(program,shader(gl,gl.VERTEX_SHADER,vertex));gl.attachShader(program,shader(gl,gl.FRAGMENT_SHADER,fragment));gl.linkProgram(program);
      if(!gl.getProgramParameter(program,gl.LINK_STATUS))throw Error(gl.getProgramInfoLog(program));
      gl.useProgram(program);
      const buffer=this.buffer=gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER,buffer);gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([-1,-1,1,-1,-1,1,-1,1,1,-1,1,1]),gl.STATIC_DRAW);
      const attribute=gl.getAttribLocation(program,'position');gl.enableVertexAttribArray(attribute);gl.vertexAttribPointer(attribute,2,gl.FLOAT,false,0,0);
      this.uniforms={};for(const name of ['resolution','time','phase','energy','expansion','weave','cool','warm','kind'])this.uniforms[name]=gl.getUniformLocation(program,name);
      canvas.addEventListener('webglcontextlost',event=>{event.preventDefault();this.lost=true;});
      canvas.addEventListener('webglcontextrestored',()=>window.location.reload());
    }
    draw(f){
      if(this.lost)return;
      const gl=this.gl,c=this.canvas,u=this.uniforms;sizeCanvas(c);gl.viewport(0,0,c.width,c.height);gl.useProgram(this.program);
      gl.uniform2f(u.resolution,c.width,c.height);
      for(const name of ['time','phase','energy','expansion','weave','cool','warm'])gl.uniform1f(u[name],f[name]);
      gl.uniform1f(u.kind,this.kind===3?1:0);gl.drawArrays(gl.TRIANGLES,0,6);
    }
    dispose(){this.gl.deleteBuffer(this.buffer);this.gl.deleteProgram(this.program);const ext=this.gl.getExtension('WEBGL_lose_context');if(ext)ext.loseContext();}
  }
  function sizeCanvas(canvas){
    const box=canvas.getBoundingClientRect(),ratio=Math.min(window.devicePixelRatio||1,1.5);
    const width=Math.max(1,Math.min(720,Math.round(box.width*ratio))),height=Math.max(1,Math.min(720,Math.round(box.height*ratio)));
    if(canvas.width!==width||canvas.height!==height){canvas.width=width;canvas.height=height;}
  }
  function prepare(canvas,ctx){
    sizeCanvas(canvas);ctx.setTransform(1,0,0,1,0,0);ctx.globalAlpha=1;ctx.globalCompositeOperation='source-over';ctx.fillStyle='#000';ctx.fillRect(0,0,canvas.width,canvas.height);
    const s=Math.min(canvas.width,canvas.height)/480;ctx.setTransform(s,0,0,s,canvas.width/2,canvas.height/2-12*s);
    ctx.lineCap='round';return ctx;
  }
  function glow(ctx,x,y,r,color,alpha){
    const g=ctx.createRadialGradient(x,y,0,x,y,r);g.addColorStop(0,`rgba(${color},${alpha})`);g.addColorStop(1,`rgba(${color},0)`);ctx.fillStyle=g;ctx.fillRect(x-r,y-r,r*2,r*2);
  }
  function palette(t,warm,alpha=1){
    const m=(Math.sin(t)+1)*.5;
    let r=mix(107,197,m),g=mix(236,157,m),b=mix(215,255,m);
    r=mix(r,255,warm*.8);g=mix(g,160,warm*.8);b=mix(b,127,warm*.8);
    return `rgba(${r|0},${g|0},${b|0},${clamp(alpha)})`;
  }
  function rotate(x,y,z,ax,ay){
    const cy=Math.cos(ay),sy=Math.sin(ay),cx=Math.cos(ax),sx=Math.sin(ax);
    const rx=x*cy+z*sy,rz=z*cy-x*sy;
    return [rx,y*cx-rz*sx,rz*cx+y*sx];
  }
  class Threads {
    constructor(canvas){this.canvas=canvas;this.ctx=canvas.getContext('2d',{alpha:false});}
    draw(f){
      const ctx=prepare(this.canvas,this.ctx),radius=119*(1+f.expansion*.06+f.energy*.1+.015*Math.sin(f.time*1.15));
      glow(ctx,-32,7,156,'117,82,211',.11);glow(ctx,35,-18,145,'70,222,198',.075);
      const pieces=[],count=58,steps=144,ax=.48+.18*Math.sin(f.phase*.14),ay=f.phase*.18;
      for(let j=0;j<count;j++){
        const latitude=(j/(count-1)-.5)*2.87,points=[];
        for(let k=0;k<=steps;k++){
          const a=k/steps*TAU,fold=Math.sin(a*3+latitude*2.6-f.phase*.55);
          const r=radius*(1+.065*fold+.025*Math.sin(a*5+f.phase+latitude*4));
          const twist=latitude+.08*Math.sin(a*2-f.phase*.3)+(f.weave*.12+f.energy*.045)*Math.sin(a*3+latitude*2+f.phase*.6);
          const p=rotate(r*Math.cos(a)*Math.cos(twist),r*Math.sin(twist),r*Math.sin(a)*Math.cos(twist),ax,ay);
          const perspective=650/(650-p[2]);points.push([p[0]*perspective,p[1]*perspective,p[2],a]);
        }
        for(let k=0;k<steps;k+=6){const vertices=points.slice(k,k+7),depth=vertices.reduce((s,p)=>s+p[2],0)/vertices.length;
          pieces.push({vertices,depth,latitude,index:j});}
      }
      pieces.sort((a,b)=>a.depth-b.depth);
      for(const piece of pieces){
        const front=clamp((piece.depth/radius+1)*.5),a=piece.vertices[0][3];
        ctx.beginPath();piece.vertices.forEach((p,i)=>i?ctx.lineTo(p[0],p[1]):ctx.moveTo(p[0],p[1]));
        ctx.lineWidth=.45+front*.68;
        ctx.strokeStyle=palette(a*.75+piece.latitude*1.5+f.phase*.13-f.cool*.6,f.warm,.10+front*.78);ctx.stroke();
        if(front>.84 && piece.index%5===0){ctx.lineWidth=.38;ctx.strokeStyle=`rgba(232,246,255,${(front-.84)*2.1})`;ctx.stroke();}
      }
      for(let i=0;i<7;i++){
        const a=f.phase*(.4+i*.016)+i*2.399,l=.9*Math.sin(i*2.7+f.phase*.13),p=rotate(radius*Math.cos(a)*Math.cos(l),radius*Math.sin(l),radius*Math.sin(a)*Math.cos(l),ax,ay);
        if(p[2]>0){const s=650/(650-p[2]),x=p[0]*s,y=p[1]*s;glow(ctx,x,y,8,'187,231,255',.15);ctx.fillStyle='rgba(225,247,255,.75)';ctx.beginPath();ctx.arc(x,y,.8,0,TAU);ctx.fill();}
      }
    }
    dispose(){}
  }
  class Corona {
    constructor(canvas){this.canvas=canvas;this.ctx=canvas.getContext('2d',{alpha:false});}
    draw(f){
      const ctx=prepare(this.canvas,this.ctx),base=114*(1+f.expansion*.075+f.energy*.09+.012*Math.sin(f.time*1.1));
      glow(ctx,-35,-15,176,'101,76,210',.09);glow(ctx,52,31,147,'75,213,201',.07);
      const gradient=ctx.createLinearGradient(-150,-90,130,115);
      gradient.addColorStop(0,f.warm>.4?'#ecb2a2':'#98fee0');gradient.addColorStop(.35,'#d1f0ff');gradient.addColorStop(.63,'#9e80ed');gradient.addColorStop(1,f.warm>.4?'#ffc08f':'#e6b4ff');
      const path=(band)=>{
        ctx.beginPath();for(let k=0;k<=200;k++){
          const a=k/200*TAU;
          const wave=Math.sin(a*3+f.phase*.48)+.4*Math.sin(a*5-f.phase*.63);
          const tide=Math.sin(a-f.phase*.75);
          const r=base+band*(13+6*tide)+wave*(3.8+f.weave*2.4)+f.energy*7*Math.sin(a*4+f.time*2);
          const x=Math.cos(a)*r,y=Math.sin(a)*r*(.93+.025*Math.sin(f.phase*.2));
          if(k)ctx.lineTo(x,y);else ctx.moveTo(x,y);
        }ctx.closePath();
      };
      ctx.strokeStyle=gradient;
      for(let i=0;i<3;i++){path((i-1)*.45);ctx.shadowBlur=18+i*8;ctx.shadowColor=i===0?'#8867d4':'#76d4d5';ctx.globalAlpha=.065;ctx.lineWidth=11;ctx.stroke();}
      ctx.shadowBlur=0;
      for(let j=0;j<62;j++){
        const band=j/61*2-1;path(band);ctx.globalAlpha=.1+.59*Math.pow(1-Math.abs(band),2);ctx.lineWidth=.65;ctx.stroke();
      }
      path(-.18);ctx.globalAlpha=.69;ctx.lineWidth=.9;ctx.strokeStyle=gradient;ctx.stroke();
      ctx.globalAlpha=1;
      for(let k=0;k<2;k++){
        const a=-f.phase*.5+k*Math.PI+.2,r=base+3.8*Math.sin(a*3+f.phase*.48),x=Math.cos(a)*r,y=Math.sin(a)*r*.94;
        glow(ctx,x,y,17,k?'181,149,254':'192,255,240',.20+f.energy*.06);
      }
      // A quiet interior makes the changing edge, not an added spinner, the focus.
      const core=ctx.createRadialGradient(0,0,35,0,0,base-18);core.addColorStop(0,'#000');core.addColorStop(.82,'#010103');core.addColorStop(1,'rgba(4,3,9,0)');
      ctx.fillStyle=core;ctx.beginPath();ctx.arc(0,0,base-18,0,TAU);ctx.fill();
    }
    dispose(){}
  }
  window.HaloMotion={create:(canvas,index)=>index===1?new Threads(canvas):index===2?new Corona(canvas):new Sculpture(canvas,index),clamp,mix};
})();
