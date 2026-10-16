// The bundled web application uses the same managed LittleFS files as /fs/NFS.
// Compiled seed assets only provide a recovery page before the volume is ready.
static void serveRuntimeAsset(const char*path,const char*type){
 if(*boardpack::RUNTIME_PATH){
  char full[256];if(snprintf(full,sizeof full,"%s%s",boardpack::RUNTIME_PATH,path)>=int(sizeof full)){http.send(414,"text/plain","path too long");return;}
  const char*relative;auto provider=vfs.resolve(full,relative);
  if(provider&&provider->available()){
   String selected=String(full)+".gz";FileInfo info;const char*rel;vfs.resolve(selected.c_str(),rel);bool compressed=!provider->stat(rel,info);
   if(!compressed){selected=full;vfs.resolve(selected.c_str(),rel);if(auto error=provider->stat(rel,info)){http.send(404,"text/plain",error);return;}}
   Vfs::Access lease(vfs,false);if(auto error=lease.add(selected.c_str(),provider->caseInsensitive())){http.send(409,"text/plain",error);return;}
   if(compressed)http.sendHeader("Content-Encoding","gzip");http.setContentLength(info.size);http.send(200,type,"");
   uint8_t bytes[1024];uint64_t at=0;auto client=http.client();
   while(at<info.size){size_t got=0;if(provider->read(rel,at,bytes,std::min<uint64_t>(sizeof bytes,info.size-at),got)||!got||client.write(bytes,got)!=got){client.stop();return;}at+=got;}
   return;
  }
 }
 for(auto asset:boardpack::ASSETS){bool compressed=strlen(asset.path)==strlen(path)+3&&!strncmp(asset.path,path,strlen(path))&&!strcmp(asset.path+strlen(path),".gz");if(compressed||!strcmp(asset.path,path)){if(compressed)http.sendHeader("Content-Encoding","gzip");http.send_P(200,type,asset.data,asset.size);return;}}
 http.send(404,"text/plain","runtime asset missing");
}
