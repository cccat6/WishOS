#include "Vfs.h"
#include <thread>
#include <atomic>
#include <cassert>
#include <iostream>
using namespace notesos;
int main(){Vfs vfs;MemoryFiles files("root","/","ram");vfs.add(files);std::atomic<bool>entered{false},done{false};std::thread worker([&]{Vfs::Access scope(vfs,true);assert(!scope.add("/record.adpcm"));assert(!scope.add("/record.adpcm.idx"));entered=true;while(!done)std::this_thread::yield();});while(!entered)std::this_thread::yield();
 assert(!vfs.lockRead("/record.adpcm",1));assert(vfs.lockRead("/unrelated",1));vfs.release(1);
 {Vfs::Access read(vfs,false);assert(!strcmp(read.add("/record.adpcm.idx"),"file_in_use"));assert(!read.add("/unrelated"));}
 assert(vfs.hasLeasesUnder("/"));done=true;worker.join();assert(!vfs.hasLeasesUnder("/"));assert(vfs.lockRead("/record.adpcm",2));
 {Vfs::Access write(vfs,true);assert(write.add("/record.adpcm"));assert(write.add("/"));}vfs.release(2);assert(vfs.canMutate("/record.adpcm"));
 std::cout<<"Cross-task read/write scopes, grouped mutation, independent paths and owner cleanup passed\n";
}
