// Pure local game rules, shared by the preview and behavioral tests.
export const directions={up:[0,-1],down:[0,1],left:[-1,0],right:[1,0]};
export function newSnake(wrap=false){return {body:[[5,5],[4,5],[3,5]],direction:'right',queued:null,food:[10,5],score:0,ended:false,won:false,wrap};}
export function turnSnake(s,d){if(!directions[d]||s.queued||s.ended)return;const a=directions[s.direction],b=directions[d];if(a[0]+b[0]===0&&a[1]+b[1]===0)return;s.queued=d;}
export function stepSnake(s,random=Math.random){
 if(s.ended)return;s.direction=s.queued??s.direction;s.queued=null;const d=directions[s.direction];let head=[s.body[0][0]+d[0],s.body[0][1]+d[1]];
 if(s.wrap)head=[(head[0]+20)%20,(head[1]+10)%10];else if(head[0]<0||head[0]>=20||head[1]<0||head[1]>=10){s.ended=true;return;}
 const eating=head[0]===s.food[0]&&head[1]===s.food[1];const occupied=eating?s.body:s.body.slice(0,-1);
 if(occupied.some(p=>p[0]===head[0]&&p[1]===head[1])){s.ended=true;return;}
 s.body.unshift(head);if(!eating)s.body.pop();else{ s.score++;const free=[];for(let y=0;y<10;y++)for(let x=0;x<20;x++)if(!s.body.some(p=>p[0]===x&&p[1]===y))free.push([x,y]);if(!free.length){s.ended=true;s.won=true;}else s.food=free[Math.min(free.length-1,Math.floor(random()*free.length))]; }
}
export function scoreWord(guess,answer){
 const marks=Array(5).fill('absent'),left=answer.split('');
 for(let i=0;i<5;i++)if(guess[i]===answer[i]){marks[i]='correct';left[i]=null;}
 for(let i=0;i<5;i++)if(marks[i]!=='correct'){const at=left.indexOf(guess[i]);if(at!==-1){marks[i]='present';left[at]=null;}}
 return marks;
}
export const wordList=['LIGHT','NIGHT','RIGHT','SIGHT','MIGHT','FIGHT','TIGHT','WATER','EARTH','PLANT','PLANE','APPLE','ALLEY','BREAD','CRANE','SLATE','TRACE','STARE','HEART','HOUSE','MOUSE','CLOUD','STORM','STONE','SNAKE','WORLD','WORDS','GAMES','DREAM','WISHY','GREEN','GREAT','GRAPE','GRASS','BRAVE','BRAIN','TRAIN','TRAIL','SMILE','SMALL','SOUND','ROUND','FOUND','BOUND','ABOUT','BEACH','SHORE','SHARE','SHARP','SPACE','SPEED','SWEET','QUIET','QUICK','WHITE','BLACK','BROWN','PAPER','RIVER','ROBOT','TABLE','TIMER','TODAY','VOICE','WATCH','WRITE','WRONG','YOUNG','STORY','STARS','FRESH','FLAME','FRAME','MUSIC','MAGIC','LEMON','CHESS','CLEAR','CLEAN','CLOSE','DANCE','DAILY','SOLAR','FOCUS','LOVED','LOVES','OTHER','THERE','THEIR','THOSE','WHERE','WHICH','THANK','THINK','THING','FIRST','AFTER','EVERY','NEVER','AGAIN','HELLO','ADIEU','AUDIO','RAISE','ROAST'];
export function newWord(answer='LIGHT'){return {answer,guesses:[],ended:false,won:false};}
export function guessWord(s,raw){const word=raw.trim().toUpperCase();if(s.ended)return 'ended';if(!/^[A-Z]{5}$/.test(word))return 'length';if(!wordList.includes(word))return 'dictionary';if(s.guesses.some(g=>g.word===word))return 'repeat';const marks=scoreWord(word,s.answer);s.guesses.push({word,marks});s.won=word===s.answer;s.ended=s.won||s.guesses.length===6;return 'ok';}
export function newFlight(){return {x:160,shots:[],hazards:[],enemies:[{x:85,y:25},{x:235,y:45}],time:0,fire:0,spawn:0,volley:0,score:0,lives:3,shield:0,ended:false,won:false};}
export function moveFlight(s,dx){s.x=Math.max(12,Math.min(308,s.x+dx));}
export function stepFlight(s,dt,random=Math.random){
 if(s.ended)return;dt=Math.max(0,Math.min(.1,dt));s.time+=dt;s.fire+=dt;s.spawn+=dt;s.volley+=dt;s.shield=Math.max(0,s.shield-dt);
 if(s.fire>=.23){s.fire=0;s.shots.push({x:s.x,y:137});}
 if(s.spawn>=1.2){s.spawn=0;s.enemies.push({x:24+random()*272,y:8});}
 if(s.volley>=.65){s.volley=0;for(const e of s.enemies)s.hazards.push({x:e.x,y:e.y+8,vx:(s.x-e.x)*.32});}
 for(const b of s.shots)b.y-=dt*160;for(const h of s.hazards){h.y+=dt*65;h.x+=h.vx*dt;}for(const e of s.enemies)e.y+=dt*11;
 for(const b of s.shots){const e=s.enemies.find(e=>!e.hit&&Math.abs(e.x-b.x)<13&&Math.abs(e.y-b.y)<10);if(e){e.hit=true;b.y=-20;s.score++;}}
 const hit=s.hazards.find(h=>Math.abs(h.x-s.x)<10&&Math.abs(h.y-143)<10)||s.enemies.find(e=>Math.abs(e.x-s.x)<17&&Math.abs(e.y-143)<13);
 if(hit&&s.shield<=0){s.lives--;s.shield=1.2;s.hazards=s.hazards.filter(h=>h!==hit);}
 s.shots=s.shots.filter(b=>b.y>-5);s.hazards=s.hazards.filter(h=>h.y<170&&h.x>-20&&h.x<340);s.enemies=s.enemies.filter(e=>!e.hit&&e.y<168);
 if(s.score>=12){s.ended=true;s.won=true;}else if(s.lives<=0)s.ended=true;
}
