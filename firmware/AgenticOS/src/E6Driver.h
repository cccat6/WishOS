#pragma once
#include <stddef.h>
#include <stdint.h>
namespace notesos {
// Portable state machine; the BSP supplies GPIO, clock and short SPI transactions.
class E6Driver {
public:
  struct IO { bool (*ready)(); void (*reset)(bool); bool (*command)(uint8_t,const uint8_t*,size_t); bool (*data)(const uint8_t*,size_t); };
  enum class Phase { Idle, ResetLow, ResetWait, Configure, Receiving, PowerOn, Trigger,
                     AwaitAssert, Refresh, PowerOff, Sleep, Completed, Fault };
  static constexpr uint32_t FRAME_BYTES=172800;
private:
  IO io_;Phase phase_=Phase::Idle;uint64_t since_=0,began_=0,deadline_=0,readySince_=0;
  uint32_t bytes_=0,timeout_=60000;uint8_t step_=0,mode_=6,resets_=1;const char* error_="none";
  bool stable(uint64_t now){if(!io_.ready()){readySince_=0;return false;}if(!readySince_)readySince_=now;return now-readySince_>=2;}
  bool cmd(uint8_t c,const uint8_t* d=nullptr,size_t n=0){return io_.ready()&&io_.command(c,d,n);}
  void move(Phase p,uint64_t now){phase_=p;since_=now;readySince_=0;deadline_=now+timeout_;}
  void fail(const char* e){error_=e;phase_=Phase::Fault;}
public:
  explicit E6Driver(IO io):io_(io){}
  Phase phase()const{return phase_;}const char* error()const{return error_;}
  bool receiving()const{return phase_==Phase::Receiving;}
  bool active()const{return phase_!=Phase::Idle&&phase_!=Phase::Completed&&phase_!=Phase::Fault;}
  bool faulted()const{return phase_==Phase::Fault;}
  uint32_t bytes()const{return bytes_;}uint64_t elapsed(uint64_t now)const{return now-began_;}
  bool start(bool accurate,uint64_t now,uint32_t timeout){
    if(active()||faulted()||!io_.ready())return false;
    timeout_=timeout;mode_=accurate?0xff:6;resets_=accurate?2:1;bytes_=0;step_=0;began_=now;error_="none";
    io_.reset(false);move(Phase::ResetLow,now);return true;
  }
  bool write(const uint8_t* p,size_t n){
    if(!receiving()||!n||n>5760||n>FRAME_BYTES-bytes_||!io_.ready())return false;
    for(size_t i=0;i<n;++i){auto a=p[i]>>4,b=p[i]&15;if(a==4||a>6||b==4||b>6)return false;}
    if(!io_.data(p,n))return false;bytes_+=n;return true;
  }
  bool commit(uint64_t now){
    if(!receiving()||bytes_!=FRAME_BYTES||!io_.ready())return false;
    if(!cmd(0x04))return false;move(Phase::PowerOn,now);return true;
  }
  // Cancellation never touches an active physical refresh or resets a faulted panel.
  bool cancel(){if(phase_!=Phase::Receiving)return false;phase_=Phase::Idle;bytes_=0;return true;}
  bool clearFault(uint64_t now){if(!faulted()||!stable(now))return false;phase_=Phase::Idle;error_="none";return true;}
  void tick(uint64_t now){
    if(!active()||receiving())return;
    if(now>deadline_){fail("display_ready_timeout");return;}
    const uint8_t zero=0,one=1,sleep=0xa5;
    switch(phase_){
    case Phase::ResetLow:if(now-since_>=(mode_==0xff?20u:5u)){io_.reset(true);move(Phase::ResetWait,now);}break;
    case Phase::ResetWait:if(now-since_>=10&&stable(now)){if(--resets_){io_.reset(false);move(Phase::ResetLow,now);}else move(Phase::Configure,now);}break;
    case Phase::Configure:
      if(!stable(now))break;
      if(step_==0){if(cmd(0xe9,&one,1))++step_;}
      else if(step_==1){if(mode_==0xff||cmd(0x30,&mode_,1))++step_;}
      else if(cmd(0x10))move(Phase::Receiving,now);break;
    case Phase::PowerOn:if(now-since_>=10&&stable(now))move(Phase::Trigger,now);break;
    case Phase::Trigger:if(cmd(0x12,&zero,1))move(Phase::AwaitAssert,now);break;
    case Phase::AwaitAssert:
      if(!io_.ready())move(Phase::Refresh,now);
      else if(now-since_>=1000)fail("refresh_busy_not_asserted");break;
    case Phase::Refresh:if(stable(now)&&cmd(0x02,&zero,1))move(Phase::PowerOff,now);break;
    case Phase::PowerOff:if(now-since_>=10&&stable(now))move(Phase::Sleep,now);break;
    case Phase::Sleep:if(cmd(0x07,&sleep,1))move(Phase::Completed,now);break;
    default:break;
    }
  }
};
}
