#pragma once
#include "Core.h"
namespace notesos {
// Display-independent foreground state. Updating a view never activates it.
// Submitted frames retain a stamp; the scheduler rejects obsolete work before
// transfer while already committed hardware work completes normally.
class PresentationState {
public:
 static constexpr unsigned Capacity=8;
 struct Stamp {uint32_t view=0,revision=0,activation=0;};
 struct View {uint32_t id=0,owner=0,revision=1;bool document=false,transient=false;};
private:
 View views_[Capacity];uint32_t next_=1,active_=0,activation_=1;Stamp presented_;
 uint32_t history_[Capacity]{};unsigned depth_=0;
 View*find(uint32_t id){for(auto&v:views_)if(id&&v.id==id)return &v;return nullptr;}
public:
 const View&at(unsigned index)const{return views_[index];}
 const View*get(uint32_t id)const{for(auto&v:views_)if(id&&v.id==id)return &v;return nullptr;}
 unsigned slot(uint32_t id)const{for(unsigned i=0;i<Capacity;++i)if(id&&views_[i].id==id)return i;return Capacity;}
 uint32_t active()const{return active_;}unsigned depth()const{return depth_;}
 Stamp stamp(uint32_t id)const{auto v=get(id);return v?Stamp{id,v->revision,id==active_?activation_:0}:Stamp{};}
 bool current(Stamp s)const{auto now=stamp(active_);return s.view&&s.view==now.view&&s.revision==now.revision&&s.activation==now.activation;}
 Stamp presented()const{return presented_;}
 void completed(Stamp s){presented_=s;} // Physical completion remains true after navigation.
 const char*access(uint32_t id,uint32_t actor)const{auto v=get(id);return !v?"view_not_found":actor&&v->owner!=actor?"view_owned_by_other":nullptr;}
 const char*create(uint32_t owner,bool document,bool transient,uint32_t&id){if(!next_)return "view_id_exhausted";for(auto&v:views_)if(!v.id){v={next_++,owner,1,document,transient};id=v.id;return nullptr;}return "view_capacity";}
 const char*validateActivation(uint32_t id,bool push,uint32_t actor)const{if(auto e=access(id,actor))return e;if(!activation_||activation_==UINT32_MAX)return "view_generation_exhausted";if(push&&active_&&active_!=id&&depth_==Capacity)return "history_capacity";return nullptr;}
 const char*activate(uint32_t id,bool push,uint32_t actor){if(auto e=validateActivation(id,push,actor))return e;if(id==active_)return nullptr;if(push&&active_)history_[depth_++]=active_;active_=id;++activation_;return nullptr;}
 const char*revise(uint32_t id,uint32_t actor,uint32_t expected=0){if(auto e=access(id,actor))return e;auto v=find(id);if(expected&&v->revision!=expected)return "view_revision_conflict";if(v->revision==UINT32_MAX)return "view_revision_exhausted";++v->revision;return nullptr;}
 const char*close(uint32_t id,uint32_t actor){if(auto e=access(id,actor))return e;*find(id)={};unsigned n=0;for(unsigned i=0;i<depth_;++i)if(history_[i]!=id)history_[n++]=history_[i];depth_=n;if(active_==id){active_=depth_?history_[--depth_]:0;if(activation_<UINT32_MAX)++activation_;}return nullptr;}
 uint32_t backTarget()const{return depth_?history_[depth_-1]:0;}
 const char*back(uint32_t actor,uint32_t&disposed){disposed=0;if(!depth_)return "navigation_empty";if(auto e=access(active_,actor))return e;if(activation_==UINT32_MAX)return "view_generation_exhausted";auto old=find(active_);uint32_t target=history_[--depth_];bool retained=old&&target==old->id;for(unsigned i=0;old&&i<depth_;++i)retained|=history_[i]==old->id;if(old&&old->transient&&!retained){disposed=old->id;*old={};}active_=target;++activation_;return nullptr;}
 void describe(JsonObject o)const{o["active_view"]=active_;o["activation"]=activation_;o["history_depth"]=depth_;o["last_presented_view"]=presented_.view;o["last_presented_revision"]=presented_.revision;o["last_presented_activation"]=presented_.activation;o["view_capacity"]=Capacity;auto a=o["views"].to<JsonArray>();for(auto&v:views_)if(v.id){auto x=a.add<JsonObject>();x["id"]=v.id;x["owner"]=v.owner;x["revision"]=v.revision;x["kind"]=v.document?"document":"surface";x["active"]=v.id==active_;}}
};
}
