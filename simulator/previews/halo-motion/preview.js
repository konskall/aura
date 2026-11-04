(() => {
  'use strict';
  const $=id=>document.getElementById(id),copy=window.HALO_MOTION_COPY,motion=window.HaloMotion;
  const keys=['idle','listening','transcribing','thinking','speaking','ready','error'];
  // pace, expansion, weave, cool, warm. State changes ease into these targets.
  const profiles={idle:[.29,0,.16,0,0],listening:[.45,.35,.3,.75,0],transcribing:[.54,-.5,.6,.32,0],thinking:[.86,-.12,1,-.3,0],speaking:[.58,.3,.46,.12,0],ready:[.32,.65,.18,.55,0],error:[.2,-.15,.12,0,1]};
  const sequence=[['idle',4],['listening',5],['transcribing',2.5],['thinking',5],['speaking',7],['ready',3]];
  const reduced=matchMedia('(prefers-reduced-motion: reduce)');
  let language='el',selected=null,focused=0,state='idle',stateAge=0,time=0,phase=0,last=0,paused=reduced.matches,cycling=!reduced.matches,volume=.65,amplitude=0;
  let values=profiles.idle.slice(),focusRenderer=null,drawCount=0,dirty=true,renderErrors=[];
  const renderers=[],visibility=new Map();
  try{const saved=JSON.parse(localStorage.getItem('aura.halo-motion.v1')||'null');if(saved?.language==='en')language='en';if(Number.isInteger(saved?.selected)&&saved.selected>=0&&saved.selected<4)selected=saved.selected;}catch{}
  const text=()=>copy[language],stage=()=>text().stagesData.find(s=>s[0]===state);
  const save=()=>{try{localStorage.setItem('aura.halo-motion.v1',JSON.stringify({language,selected}));}catch{}};
  function element(tag,className,content){const e=document.createElement(tag);if(className)e.className=className;if(content!==undefined)e.textContent=content;return e;}
  function button(className,content,handler){const b=element('button',className,content);b.type='button';b.addEventListener('click',handler);return b;}
  function handleFailure(canvas,error){renderErrors.push(String(error));const message=element('p','render-failure',text().unavailable);canvas.parentElement.append(message);}
  function buildCards(){
    for(let index=0;index<4;index++){
      const card=element('article','concept');card.dataset.concept=index;
      const top=element('div','concept-top');top.append(element('span','concept-number',String(index+1).padStart(2,'0')),element('span','selected-mark',''));
      const view=button('canvas-button','',()=>openFocus(index));view.dataset.view=index;
      const canvas=element('canvas');canvas.width=360;canvas.height=360;canvas.setAttribute('role','img');view.append(canvas);
      const body=element('div','concept-body');body.append(element('p','material'),element('h2'),element('p','description'));
      const actions=element('div','concept-actions');const large=button('quiet','',()=>openFocus(index));large.dataset.large=index;
      const choose=button('choose','',()=>select(index));choose.dataset.choose=index;
      actions.append(large,choose);body.append(actions);card.append(top,view,body);$('concepts').append(card);
      try{renderers.push({canvas,renderer:motion.create(canvas,index)});}catch(error){handleFailure(canvas,error);}
      visibility.set(canvas,true);
    }
    const observer=new IntersectionObserver(entries=>{for(const entry of entries)visibility.set(entry.target,entry.isIntersecting);dirty=true;},{rootMargin:'100px'});
    renderers.forEach(item=>observer.observe(item.canvas));
  }
  function buildStateButtons(parent){
    parent.replaceChildren();parent.setAttribute('aria-label',text().stages);
    for(const item of text().stagesData){const b=button('',item[1],()=>setState(item[0],true));b.dataset.state=item[0];b.setAttribute('aria-pressed',String(state===item[0]));parent.append(b);}
  }
  function localize(){
    const t=text();document.documentElement.lang=language;document.title=t.title;
    document.querySelectorAll('[data-copy]').forEach(e=>e.textContent=t[e.dataset.copy]);$('language').textContent=t.language;
    buildStateButtons($('states'));buildStateButtons($('focus-states'));
    document.querySelectorAll('.concept').forEach((card,index)=>{
      const c=t.concepts[index];card.querySelector('.material').textContent=c.material;card.querySelector('h2').textContent=c.name;card.querySelector('.description').textContent=c.description;
      card.querySelector('.canvas-button').setAttribute('aria-label',`${t.view}: ${c.name}`);card.querySelector('canvas').setAttribute('aria-label',`${c.name} · ${t.demo}`);
      card.querySelector('[data-large]').textContent=t.view;
    });
    $('voice').setAttribute('aria-label',t.voice);$('voice').title=t.voiceHint;
    $('close-focus').setAttribute('aria-label',t.close);$('previous-concept').setAttribute('aria-label',t.previous);$('next-concept').setAttribute('aria-label',t.next);
    updateSelection();updateStateUI();updateFocus();dirty=true;
  }
  function updateStateUI(){
    const t=text();document.querySelectorAll('[data-state]').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.state===state)));
    $('state-caption').textContent=stage()[3];$('device-state').textContent=stage()[1];$('device-line').textContent=stage()[2];
    $('cycle').setAttribute('aria-pressed',String(cycling));$('pause').textContent=paused?t.play:t.pause;$('pause').setAttribute('aria-pressed',String(paused));document.body.classList.toggle('paused',paused);
    $('focus-cycle').setAttribute('aria-pressed',String(cycling));$('focus-pause').textContent=paused?t.play:t.pause;$('focus-pause').setAttribute('aria-pressed',String(paused));
    $('focus-canvas').setAttribute('aria-label',`${t.concepts[focused].name} · ${stage()[1]} · ${t.demo}`);
  }
  function setState(next,manual=false){
    if(!keys.includes(next))return;state=next;stateAge=0;if(manual)cycling=false;
    if(paused){values=profiles[state].slice();amplitude=(state==='listening'||state==='speaking')?envelope(time):0;}updateStateUI();dirty=true;
  }
  function updateSelection(){
    const t=text();document.querySelectorAll('.concept').forEach((card,index)=>{
      const active=selected===index;card.classList.toggle('selected',active);card.querySelector('.selected-mark').textContent=active?'✓':'';
      const b=card.querySelector('[data-choose]');b.textContent=active?t.chosen:t.choose;b.setAttribute('aria-pressed',String(active));
    });
    $('preference').hidden=selected===null;$('preference-name').textContent=selected===null?'':`${t.selected} ${t.concepts[selected].name}`;
    $('focus-choose').textContent=selected===focused?t.chosen:t.choose;$('focus-choose').setAttribute('aria-pressed',String(selected===focused));
  }
  function select(index){selected=index;updateSelection();save();}
  function updateFocus(){
    const c=text().concepts[focused];$('focus-number').textContent=String(focused+1).padStart(2,'0')+' / 04';$('focus-material').textContent=c.material;$('focus-name').textContent=c.name;$('focus-detail').textContent=c.detail;
    $('focus-dialog').setAttribute('aria-label',`${c.name} · ${text().frame}`);$('focus-canvas').setAttribute('aria-label',`${c.name} · ${stage()[1]} · ${text().demo}`);updateSelection();
  }
  function openFocus(index){
    focused=(index+4)%4;if(focusRenderer){focusRenderer.dispose();focusRenderer=null;}
    // A fresh canvas permits switching between a 2D and a WebGL renderer.
    const old=$('focus-canvas'),canvas=element('canvas');canvas.id='focus-canvas';canvas.width=480;canvas.height=480;canvas.setAttribute('role','img');old.replaceWith(canvas);
    $('device').querySelectorAll('.render-failure').forEach(e=>e.remove());
    if(!$('focus-dialog').open)$('focus-dialog').showModal();
    try{focusRenderer=motion.create(canvas,focused);}catch(error){handleFailure(canvas,error);}
    updateFocus();updateStateUI();dirty=true;
  }
  function envelope(t){
    const phrase=t%4.9;
    const gate=phrase<3.55?Math.pow(Math.sin(Math.PI*phrase/3.55),.65):0;
    const syllable=.27+.46*Math.pow(Math.sin(t*6.8),2)+.27*Math.pow(Math.sin(t*10.6+.8),2);
    return gate*syllable*volume;
  }
  function frame(now){
    const dt=last?Math.min(.05,(now-last)/1000):0;last=now;
    if(!paused&&!document.hidden){
      time+=dt;stateAge+=dt;
      if(cycling){const index=Math.max(0,sequence.findIndex(s=>s[0]===state)),duration=sequence[index][1];if(stateAge>=duration)setState(sequence[(index+1)%sequence.length][0]);}
      const smoothing=1-Math.exp(-dt*3.6);values=values.map((v,i)=>motion.mix(v,profiles[state][i],smoothing));phase+=dt*values[0];
      const target=(state==='listening'||state==='speaking')?envelope(time*(state==='speaking'?1:.78)):0;
      amplitude=motion.mix(amplitude,target,1-Math.exp(-dt*(target>amplitude?11:5)));dirty=true;
    }
    if(dirty&&!document.hidden){
      const f={time,phase,energy:amplitude,expansion:values[1],weave:values[2],cool:values[3],warm:values[4]};
      if($('focus-dialog').open){if(focusRenderer)focusRenderer.draw(f);}
      else for(const item of renderers)if(visibility.get(item.canvas))item.renderer.draw(f);
      const duration=sequence.find(s=>s[0]===state)?.[1]||4;$('journey-progress').style.width=cycling?`${motion.clamp(stateAge/duration)*100}%`:'0%';
      drawCount++;dirty=false;
    }
    requestAnimationFrame(frame);
  }
  buildCards();localize();
  $('language').addEventListener('click',()=>{language=language==='el'?'en':'el';save();localize();});
  const toggleCycle=()=>{cycling=!cycling;if(cycling){paused=false;setState('idle');}updateStateUI();};
  const togglePause=()=>{paused=!paused;updateStateUI();dirty=true;};
  $('cycle').addEventListener('click',toggleCycle);$('focus-cycle').addEventListener('click',toggleCycle);
  $('pause').addEventListener('click',togglePause);$('focus-pause').addEventListener('click',togglePause);
  $('voice').addEventListener('input',event=>{volume=Number(event.target.value)/100;$('voice-value').value=Math.round(volume*100)+'%';if(paused){amplitude=(state==='listening'||state==='speaking')?volume*.55:0;dirty=true;}});
  $('close-focus').addEventListener('click',()=>$('focus-dialog').close());
  $('focus-dialog').addEventListener('close',()=>{if(focusRenderer){focusRenderer.dispose();focusRenderer=null;}dirty=true;});
  $('focus-choose').addEventListener('click',()=>select(focused));
  $('previous-concept').addEventListener('click',()=>openFocus(focused-1));$('next-concept').addEventListener('click',()=>openFocus(focused+1));
  document.addEventListener('keydown',event=>{if($('focus-dialog').open&&event.target.tagName!=='INPUT'){if(event.key==='ArrowRight')openFocus(focused+1);if(event.key==='ArrowLeft')openFocus(focused-1);}});
  window.addEventListener('resize',()=>dirty=true);document.addEventListener('visibilitychange',()=>{last=0;dirty=true;});
  reduced.addEventListener('change',event=>{if(event.matches){paused=true;cycling=false;updateStateUI();dirty=true;}});
  // Read-only diagnostics plus presentation controls for local visual review.
  window.haloPreview={get snapshot(){return {language,state,paused,cycling,selected,focused,open:$('focus-dialog').open,time,phase,drawCount,renderErrors:[...renderErrors]};},setState,openFocus,select,
    pause(value=true){paused=value;updateStateUI();dirty=true;},seek(seconds){time=seconds;phase=seconds*.6;stateAge=0;values=profiles[state].slice();amplitude=(state==='listening'||state==='speaking')?envelope(time):0;dirty=true;}};
  requestAnimationFrame(frame);
})();
