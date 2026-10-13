#pragma once
#include "E6Driver.h"
#include "E6MonoCache.h"
#include "E6Waveforms.h"
#include <string.h>
namespace notesos {
// Fixed compound presentations. The caller supplies a replayable image in bands;
// this driver owns transitions, controller RAM and all BUSY/power sequencing.
class E6ColorDriver {
public:
 enum class Mode:uint8_t {BlueYellow,Fast,Quality};
 enum class Frame:uint8_t {None,Zero,High,Low,Mono,MonoReturn};
 enum class Phase:uint8_t {Idle,SafeOff,ResetLow,ResetHigh,Configure,Load,FrameStart,Receiving,PowerOn,Trigger,AfterDrive,PowerOff,WaitLow,WaitHigh,Delay,Next,Completed,Fault};
 static constexpr size_t FRAME_BYTES=172800,MONO_BYTES=43200;
private:
 enum class Optical:uint8_t {Unknown,Base,Mono,Fast,Quality};
 struct Stage{int8_t wave=-1;Frame frame=Frame::None;bool cycle=true,off=false;};
 E6Driver::IO io_;Stage stages_[6];unsigned count_=0,stage_=0,config_=0,resets_=0;
 Phase phase_=Phase::Idle,after_=Phase::Idle;Optical optical_=Optical::Unknown;
 Mode mode_=Mode::Fast;bool powered_=false,offWait_=false;E6MonoCache*mono_=nullptr;uint8_t monoBand_[E6MonoCache::BAND_BYTES]{};
 size_t bytes_=0;uint32_t timeout_=60000,delay_=0;uint64_t since_=0,deadline_=0,readySince_=0;
 const char*error_="none";
 bool cmd(uint8_t c,const uint8_t*p=nullptr,size_t n=0){return io_.ready()&&io_.command(c,p,n);}
 bool stable(uint64_t now){if(!io_.ready()){readySince_=0;return false;}if(!readySince_)readySince_=now;return now-readySince_>=2;}
 void move(Phase phase,uint64_t now){phase_=phase;since_=now;readySince_=0;deadline_=now+timeout_;}
 void add(e6::Wave wave,Frame frame,bool off=false){stages_[count_++]={int8_t(wave),frame,true,off};}
 void wait(Phase after,uint32_t delay,uint64_t now,bool off=false){after_=after;delay_=delay;offWait_=off;move(Phase::WaitLow,now);}
 void fail(const char*error){error_=error;optical_=Optical::Unknown;phase_=Phase::Fault;}
public:
 explicit E6ColorDriver(E6Driver::IO io):io_(io){}
 bool active()const{return phase_!=Phase::Idle&&phase_!=Phase::Completed&&phase_!=Phase::Fault;}
 bool faulted()const{return phase_==Phase::Fault;}
 bool safeToIdle()const{return !active()&&!faulted()&&io_.ready();}
 bool completed()const{return phase_==Phase::Completed;}
 bool receiving()const{return phase_==Phase::Receiving;}
 Frame frame()const{return stages_[stage_].frame;}
 size_t bytes()const{return bytes_;}
 const char*error()const{return error_;}
 bool needsPreviousMono()const{return optical_==Optical::Mono;}
 void invalidate(){optical_=Optical::Unknown;}
 bool clearFault(uint64_t now){if(!faulted()||!stable(now))return false;phase_=Phase::Idle;error_="none";return true;}
 bool start(Mode mode,E6MonoCache*mono,uint64_t now,uint32_t timeout){
  if(active()||faulted()||!io_.ready())return false;
  if((mode==Mode::BlueYellow||optical_==Optical::Mono)&&!mono)return false;
  mode_=mode;mono_=mono;timeout_=timeout;count_=stage_=config_=bytes_=0;error_="none";
  bool reset=optical_==Optical::Unknown;
  if(optical_==Optical::Unknown||optical_==Optical::Quality){add(e6::Wave::Clear,Frame::Zero,true);if(mono_)mono_->clear();}
  else if(optical_==Optical::Fast){add(e6::Wave::FastReturn,Frame::None);if(mono_)mono_->clear();}
  else if(optical_==Optical::Mono&&mode!=Mode::BlueYellow)add(e6::Wave::MonoReturn,Frame::MonoReturn);
  if(mode==Mode::BlueYellow)add(e6::Wave::Mono,Frame::Mono,true);
  else if(mode==Mode::Fast){add(e6::Wave::FastBase,Frame::High);add(e6::Wave::Tune,Frame::Low,true);stages_[count_++]={-1,Frame::High,false,false};}
  else{add(e6::Wave::QualityBalance,Frame::High);add(e6::Wave::QualityBase,Frame::None);add(e6::Wave::Tune,Frame::Low,true);}
  resets_=2;
  if(reset){if(powered_)move(Phase::SafeOff,now);else{io_.reset(false);move(Phase::ResetLow,now);}}
  else move(Phase::Load,now);
  return true;
 }
 bool write(uint8_t*data,size_t size,uint64_t now){
  if(!receiving()||!size||size>5760||size>FRAME_BYTES-bytes_||size>5760-bytes_%5760||!io_.ready())return false;
  // Retain the target only after the SPI write succeeds. The same input band
  // can be retried when another peripheral temporarily owns the bus.
  bool mono=frame()==Frame::Mono||frame()==Frame::MonoReturn;
  if(mono&&bytes_%5760==0)mono_->read(bytes_/5760,monoBand_);
  if(mono)for(size_t i=0;i<size;++i){uint8_t value=0;for(unsigned side=0;side<2;++side){size_t pixel=((bytes_%5760)+i)*2+side;unsigned shift=side?0:4;uint8_t next=frame()==Frame::MonoReturn?0:((data[i]>>shift)&1),old=(monoBand_[pixel/8]>>(7-pixel%8))&1;value|=((old<<1)|next)<<shift;}data[i]=value;}
  bool accepted=io_.data(data,size);
  if(mono)for(size_t i=0;i<size;++i){if(accepted)for(unsigned side=0;side<2;++side){size_t pixel=((bytes_%5760)+i)*2+side;uint8_t mask=1u<<(7-pixel%8),next=(data[i]>>(side?0:4))&1;if(next)monoBand_[pixel/8]|=mask;else monoBand_[pixel/8]&=~mask;}data[i]&=0x11;}
  if(!accepted)return false;
  bytes_+=size;if(mono&&bytes_%5760==0&&!mono_->write(bytes_/5760-1,monoBand_)){sourceFailed("mono_cache_memory_low",now);return true;}if(bytes_==FRAME_BYTES)move(stages_[stage_].cycle?Phase::PowerOn:Phase::Next,now);return true;
 }
 // A missing file/card can occur between phases. Finish a safe power-off at a
 // ready boundary, rather than abandoning a powered controller or resetting BUSY.
 void sourceFailed(const char*error,uint64_t now){error_=error;optical_=Optical::Unknown;if(powered_){after_=Phase::Fault;move(Phase::PowerOff,now);}else fail(error);}
 void tick(uint64_t now){
  if(!active())return;
  if(now>deadline_){fail("display_ready_timeout");return;}
  if(receiving())return;
  const uint8_t zero=0;
  switch(phase_){
   case Phase::SafeOff:if(cmd(0x02,&zero,1))wait(Phase::ResetLow,200,now,true);break;
   case Phase::ResetLow:if(now-since_>=20){io_.reset(true);move(Phase::ResetHigh,now);}break;
   case Phase::ResetHigh:if(now-since_>=10&&stable(now)){if(--resets_){io_.reset(false);move(Phase::ResetLow,now);}else move(Phase::Configure,now);}break;
   case Phase::Configure:{static constexpr uint8_t reg[]={0xe9,0x30,0x41,0xe0,0xe6},value[]={1,4,0,2,25};if(cmd(reg[config_],value+config_,1)&&++config_==5)move(Phase::Load,now);break;}
   case Phase::Load:{const auto&s=stages_[stage_];if(!io_.ready())break;if(s.wave>=0){const uint8_t psr[]={0x0f,0xa9};if(!cmd(0x00,psr,2)||!cmd(0x20,e6::waves[unsigned(s.wave)],799))break;}move(s.frame==Frame::None?Phase::PowerOn:Phase::FrameStart,now);break;}
   case Phase::FrameStart:if(cmd(0x10)){bytes_=0;move(Phase::Receiving,now);}break;
   case Phase::PowerOn:if(powered_)move(Phase::Trigger,now);else if(cmd(0x04)){powered_=true;wait(Phase::Trigger,100,now);}break;
   case Phase::Trigger:if(cmd(0x12,&zero,1))wait(Phase::AfterDrive,0,now);break;
   case Phase::AfterDrive:if(stages_[stage_].off){after_=Phase::Next;move(Phase::PowerOff,now);}else move(Phase::Next,now);break;
   case Phase::PowerOff:if(cmd(0x02,&zero,1))wait(after_,200,now,true);break;
   case Phase::WaitLow:if(!io_.ready())move(Phase::WaitHigh,now);else if(now-since_>=1000)fail("display_busy_not_asserted");break;
   case Phase::WaitHigh:if(stable(now)){if(offWait_)powered_=false;move(Phase::Delay,now);}break;
   case Phase::Delay:if(now-since_>=delay_){if(after_==Phase::ResetLow)io_.reset(false);move(after_,now);}break;
   case Phase::Next:if(++stage_==count_){optical_=mode_==Mode::BlueYellow?Optical::Mono:mode_==Mode::Fast?Optical::Fast:Optical::Quality;move(Phase::Completed,now);}else move(Phase::Load,now);break;
   default:break;
  }
 }
};
}
