#include "Lz4.h"
#include <cassert>
#include <fstream>
#include <random>
#include <vector>
#include <iostream>
int main(int argc,char**argv){std::mt19937 random(471);std::ofstream vectors;if(argc==2)vectors.open(argv[1],std::ios::binary);
 for(unsigned size:{0,1,4,12,13,255,256,511,512,1024,4096,8192,65535})for(unsigned mode=0;mode<4;++mode){std::vector<uint8_t> raw(size),encoded(nm::lz4Bound(size)+1,0xa5),decoded(size+1,0);
  for(auto&byte:raw)byte=mode==0?'x':mode==1?random()%4:mode==2?random()%64:random()%256;
  assert(!nm::lz4Pack(raw.data(),size,encoded.data(),nm::lz4Bound(size)-1));assert(encoded.back()==0xa5);
  size_t stored=nm::lz4Pack(raw.data(),size,encoded.data(),nm::lz4Bound(size));assert(stored&&stored<=nm::lz4Bound(size)&&encoded.back()==0xa5);assert(nm::lz4Raw(encoded.data(),stored,decoded.data(),size));assert(!size||!memcmp(raw.data(),decoded.data(),size));
  if(vectors){uint32_t lengths[]={size,uint32_t(stored)};vectors.write((char*)lengths,sizeof lengths);vectors.write((char*)raw.data(),size);vectors.write((char*)encoded.data(),stored);}
 }
 std::cout<<"Small-metadata LZ4 roundtrips, zero/short/maximum inputs and bounded output passed\n";
}
