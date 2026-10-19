import assert from 'node:assert/strict';
import {newSnake,turnSnake,stepSnake,scoreWord,newWord,guessWord,newFlight,moveFlight,stepFlight} from '../public/game-engine.js';
// Direction changes must never reverse into the body, including rapid key presses.
const snake=newSnake();turnSnake(snake,'left');stepSnake(snake);assert.deepEqual(snake.body[0],[6,5]);turnSnake(snake,'up');turnSnake(snake,'left');stepSnake(snake);assert.deepEqual(snake.body[0],[6,4]);
const eating=newSnake();eating.food=[6,5];stepSnake(eating,()=>0);assert.equal(eating.score,1);assert.equal(eating.body.length,4);assert.ok(!eating.body.some(p=>p.join()===eating.food.join()));
const wall=newSnake();wall.body=[[19,5],[18,5],[17,5]];stepSnake(wall);assert.equal(wall.ended,true);const frozen=JSON.stringify(wall);stepSnake(wall);assert.equal(JSON.stringify(wall),frozen);
const wrap=newSnake(true);wrap.body=[[19,5],[18,5],[17,5]];stepSnake(wrap);assert.deepEqual(wrap.body[0],[0,5]);assert.equal(wrap.ended,false);
const tail=newSnake();tail.body=[[1,1],[1,2],[2,2],[2,1]];tail.direction='right';stepSnake(tail);assert.equal(tail.ended,false,'Vacating tail is a legal move');
const self=newSnake();self.body=[[1,1],[2,1],[2,2],[1,2],[0,2]];self.direction='down';stepSnake(self);assert.equal(self.ended,true);
assert.deepEqual(scoreWord('ALLEY','APPLE'),['correct','present','absent','present','absent'],'Repeated letters use only the remaining letter count');
const word=newWord();assert.equal(guessWord(word,'xx'),'length');assert.equal(guessWord(word,'ZZZZZ'),'dictionary');assert.equal(word.guesses.length,0);assert.equal(guessWord(word,'stare'),'ok');assert.equal(guessWord(word,'STARE'),'repeat');assert.equal(guessWord(word,'light'),'ok');assert.equal(word.won,true);assert.equal(guessWord(word,'NIGHT'),'ended');
const lost=newWord();for(const guess of ['STARE','CLOUD','PLANT','DREAM','WATER','APPLE'])guessWord(lost,guess);assert.equal(lost.ended,true);assert.equal(lost.won,false);assert.equal(lost.guesses.length,6);
const flight=newFlight();moveFlight(flight,-999);assert.equal(flight.x,12);moveFlight(flight,999);assert.equal(flight.x,308);flight.x=160;flight.hazards=[{x:160,y:140,vx:0},{x:160,y:140,vx:0}];stepFlight(flight,.01);assert.equal(flight.lives,2);stepFlight(flight,.01);assert.equal(flight.lives,2,'Invulnerability prevents one volley draining every life');
const hit=newFlight();hit.shots=[{x:85,y:25}];stepFlight(hit,0);assert.equal(hit.score,1);assert.equal(hit.enemies.length,1);assert.equal(hit.shots.length,0);
const winner=newFlight();winner.score=11;winner.shots=[{x:85,y:25}];stepFlight(winner,0);assert.equal(winner.won,true);const result=JSON.stringify(winner);stepFlight(winner,.05);assert.equal(JSON.stringify(winner),result);
console.log('Verified snake input/collisions/wrap/food, repeated-letter scoring and guess limits, flight boundaries/collisions/shield/victory.');
