#pragma once
#include "NfsProvider.h"
#include "ResourceBudget.h"
#include <new>
namespace notesos {
struct NfsOptions {
 char provider[49]="user",path[64]="/sd";
 esp32_nfs::Config wire;esp32_nfs::MemoryLimits memory{65536,32768};
 static const char*parse(JsonObjectConst j,NfsOptions&o){
   struct Range{const char*key;uint32_t min,max;};
   for(auto r:{Range{"port",1,65535},Range{"block_size",512,32768},Range{"bytes_per_second",0,UINT32_MAX},Range{"io_bytes_per_poll",1,262144},Range{"directory_entries",1,256},Range{"connections",1,ESP32_NFS_CONNECTION_COUNT},Range{"idle_timeout_ms",1,INT32_MAX},Range{"record_timeout_ms",1,INT32_MAX},Range{"replay_window_ms",1,INT32_MAX}}){if(j.containsKey(r.key)&&(!j[r.key].is<uint32_t>()||j[r.key].as<uint32_t>()<r.min||j[r.key].as<uint32_t>()>r.max))return "invalid_nfs_config";}
   if(j.containsKey("read_only")&&!j["read_only"].is<bool>())return "invalid_nfs_config";
   const char*p=j["provider"]|"user",*path=j["export_path"]|"/sd";
   char normal[64];if(!*p||strlen(p)>=sizeof(o.provider))return "invalid_provider";
   if(!normalizePath("/",path,normal,sizeof(normal))||strcmp(path,normal)||strchr(path,' '))return "invalid_export_path";
   strcpy(o.provider,p);strcpy(o.path,path);
   if((j.containsKey("memory_max_bytes")&&!j["memory_max_bytes"].is<uint32_t>())||(j.containsKey("heap_reserve_bytes")&&!j["heap_reserve_bytes"].is<uint32_t>()))return "invalid_nfs_memory";
   o.memory.maxBytes=j["memory_max_bytes"]|65536u;o.memory.reserveBytes=j["heap_reserve_bytes"]|32768u;if(o.memory.maxBytes<4096||o.memory.maxBytes>8388608||o.memory.reserveBytes>8388608)return "invalid_nfs_memory";
   auto&c=o.wire;c.readOnly=j["read_only"]|false;c.port=j["port"]|2049u;
   c.blockSize=j["block_size"]|4096u;c.bytesPerSecond=j["bytes_per_second"]|131072u;
   c.ioBytesPerPoll=j["io_bytes_per_poll"]|2048u;c.directoryEntriesPerRpc=j["directory_entries"]|4u;
   c.connections=j["connections"]|2u;c.idleTimeoutMs=j["idle_timeout_ms"]|30000u;
   c.recordTimeoutMs=j["record_timeout_ms"]|5000u;c.replayWindowMs=j["replay_window_ms"]|60000u;
   if(!c.port||c.port==80||c.port==53||c.port==111||c.blockSize<512||c.blockSize>esp32_nfs::MaxBlockSize||!c.ioBytesPerPoll||!c.directoryEntriesPerRpc||!c.connections||c.connections>ESP32_NFS_CONNECTION_COUNT||!c.idleTimeoutMs||!c.recordTimeoutMs||!c.replayWindowMs)return "invalid_nfs_config";
   return nullptr;
 }
 void describe(JsonObject o)const{
   o["memory_max_bytes"]=memory.maxBytes;o["heap_reserve_bytes"]=memory.reserveBytes;
   o["provider"]=provider;o["export_path"]=path;o["read_only"]=wire.readOnly;o["port"]=wire.port;
   o["block_size"]=wire.blockSize;o["bytes_per_second"]=wire.bytesPerSecond;o["io_bytes_per_poll"]=wire.ioBytesPerPoll;
   o["directory_entries"]=wire.directoryEntriesPerRpc;o["connections"]=wire.connections;
   o["idle_timeout_ms"]=wire.idleTimeoutMs;o["record_timeout_ms"]=wire.recordTimeoutMs;o["replay_window_ms"]=wire.replayWindowMs;
 }
};
// Transport is owned by the application; one cooperative poll shares the OS loop.
class NfsService {
 struct Session {
   NfsOptions options;esp32_nfs::Memory memory;esp32_nfs::PathVfs::Registry registry;NfsProviderStorage storage;NfsProviderVfs fs;esp32_nfs::Server server;
   Session(const NfsOptions&o,FileProvider&p,Vfs&v,const esp32_nfs::Memory::Allocator&allocator):options(o),memory(o.memory,allocator),registry(memory),storage(p,v),fs(storage,p,registry),server(fs,nullptr,memory){}
 };
 Vfs&vfs_;ResourceBudget&budget_;Session*session_=nullptr;static constexpr uint32_t OWNER=0x4e4653;
 void destroy(Session*p){p->~Session();budget_.freeAllocation(p);}
 uint32_t invalidations_=0;bool stopping_=false;esp32_nfs::Status stopStatus_=esp32_nfs::Ok;
public:
 explicit NfsService(Vfs&v,ResourceBudget&budget):vfs_(v),budget_(budget){}
 ~NfsService(){stop();}
 bool active()const{return session_!=nullptr;}
 bool uses(const char*id)const{return session_&&!strcmp(id,session_->options.provider);}
 const char*start(const NfsOptions&o,esp32_nfs::Transport&t,uint64_t epoch){
   if(active())return "nfs_already_running";auto p=vfs_.get(o.provider);if(!p)return "unknown_provider";
   if(!p->available())return "provider_unavailable";
   if(!o.wire.readOnly&&!p->writable)return "read_only";
   // Never promise COMMIT persistence for RAM. Volatile providers are exported read-only.
   if(!o.wire.readOnly&&!p->durableWrites())return "read_write_requires_durable_provider";
   esp32_nfs::Memory::Allocator allocator;allocator.context=&budget_;allocator.allocate=[](void*ctx,size_t n){return static_cast<ResourceBudget*>(ctx)->allocate(n);};allocator.release=[](void*ctx,void*p){static_cast<ResourceBudget*>(ctx)->freeAllocation(p);};allocator.available=[](void*ctx)->size_t{return static_cast<ResourceBudget*>(ctx)->allocatableHeap(ResourceBudget::Region::Auto);};
   auto block=budget_.allocate(sizeof(Session));if(!block)return "memory_low";auto next=new(block) Session(o,*p,vfs_,allocator);
   if(!vfs_.pin(*p,OWNER)){destroy(next);return "service_lease_capacity";}
   auto config=o.wire;config.exportPath=next->options.path;
   if(!next->server.begin(t,config,epoch)){vfs_.unpin(OWNER);destroy(next);return "nfs_listen_failed";}
   session_=next;invalidations_=0;stopping_=false;stopStatus_=esp32_nfs::Ok;return nullptr;
 }
 const char*stop(){if(session_){stopping_=true;stopStatus_=session_->server.stop();if(stopStatus_!=esp32_nfs::Ok)return "nfs_stop_pending";destroy(session_);session_=nullptr;}stopping_=false;vfs_.unpin(OWNER);return nullptr;}
 const char*memory(JsonObjectConst args,JsonObject out){
   if(!session_||stopping_)return "nfs_not_running";
   for(auto field:args)if(strcmp(field.key().c_str(),"memory_max_bytes")&&strcmp(field.key().c_str(),"heap_reserve_bytes"))return "unknown_nfs_memory_setting";
   JsonDocument snapshot;session_->options.describe(snapshot.to<JsonObject>());for(auto field:args)snapshot[field.key()]=field.value();NfsOptions next;if(auto e=NfsOptions::parse(snapshot.as<JsonObjectConst>(),next))return e;
   if(!session_->memory.configure(next.memory))return "nfs_memory_budget_in_use";session_->options.memory=next.memory;read(out);return nullptr;
 }
 // Returns true if a local writer invalidated client handles. Clients must remount.
 bool poll(uint32_t now){if(!session_||stopping_)return false;bool changed=session_->storage.reconcile();
   if(changed)++invalidations_;session_->server.poll(now);return changed;}
 void read(JsonObject o)const{
   o["state"]=stopping_?"stop_pending":active()?"running":"stopped";o["running"]=active()&&!stopping_;o["stop_status"]=uint32_t(stopStatus_);o["library_version"]="1.3.0";o["protocol"]="nfsv3_tcp";
   o["workspace_bound_bytes"]=esp32_nfs::Server::activeWorkspaceBytes(session_?session_->options.wire.blockSize:4096);o["external_invalidations"]=invalidations_;
   o["owned_heap_bytes"]=0;o["path_registry_bytes"]=0;o["memory_used_bytes"]=0;o["memory_allocations"]=0;o["path_identities"]=0;
   if(!session_)return;session_->options.describe(o["active"].to<JsonObject>());
   auto&m=session_->server.metrics();o["received_bytes"]=m.received;o["sent_bytes"]=m.sent;
   o["requests"]=m.requests;o["replay_hits"]=m.replayHits;o["rejected"]=m.rejected;o["timeouts"]=m.timeouts;
   o["path_registry_bytes"]=session_->fs.registryBytes();auto memory=session_->memory.stats();o["owned_heap_bytes"]=sizeof(Session)+memory.usedBytes;
   o["memory_used_bytes"]=memory.usedBytes;o["memory_peak_bytes"]=memory.peakBytes;o["memory_allocations"]=memory.allocations;o["memory_failures"]=memory.failures;
   const char*errors[]={"none","budget","reserve","allocation","invalid","busy"};o["memory_last_error"]=errors[unsigned(memory.lastError)];o["path_identities"]=session_->fs.identityCount();
   o["heap_accounting"]="service_and_protocol_allocations_excludes_socket_stack";
 }
};
}
