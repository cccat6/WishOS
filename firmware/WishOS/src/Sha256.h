#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace notesos {
class Sha256 {
 uint32_t h_[8];uint64_t bytes_=0;uint8_t block_[64]{};size_t used_=0;
 static uint32_t r(uint32_t x,unsigned n){return (x>>n)|(x<<(32-n));}
 void compress(){static const uint32_t k[]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};uint32_t w[64];for(unsigned i=0;i<16;++i)w[i]=uint32_t(block_[i*4])<<24|uint32_t(block_[i*4+1])<<16|uint32_t(block_[i*4+2])<<8|block_[i*4+3];for(unsigned i=16;i<64;++i)w[i]=w[i-16]+(r(w[i-15],7)^r(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(r(w[i-2],17)^r(w[i-2],19)^(w[i-2]>>10));uint32_t a=h_[0],b=h_[1],c=h_[2],d=h_[3],e=h_[4],f=h_[5],g=h_[6],h=h_[7];for(unsigned i=0;i<64;++i){uint32_t t=h+(r(e,6)^r(e,11)^r(e,25))+((e&f)^(~e&g))+k[i]+w[i],u=(r(a,2)^r(a,13)^r(a,22))+((a&b)^(a&c)^(b&c));h=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;}h_[0]+=a;h_[1]+=b;h_[2]+=c;h_[3]+=d;h_[4]+=e;h_[5]+=f;h_[6]+=g;h_[7]+=h;}
public:
 Sha256(){reset();}void reset(){const uint32_t h[]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};memcpy(h_,h,sizeof h);bytes_=used_=0;}
 void update(const uint8_t*p,size_t n){bytes_+=n;while(n){size_t z=n<64-used_?n:64-used_;memcpy(block_+used_,p,z);p+=z;n-=z;used_+=z;if(used_==64){compress();used_=0;}}}
 void finish(char out[65])const{auto s=*this;uint64_t bits=bytes_*8;uint8_t b=0x80;s.update(&b,1);b=0;while(s.used_!=56)s.update(&b,1);uint8_t tail[8];for(unsigned i=0;i<8;++i)tail[7-i]=bits>>(i*8);s.update(tail,8);const char*x="0123456789abcdef";for(unsigned i=0;i<32;++i){uint8_t v=s.h_[i/4]>>((3-i%4)*8);out[i*2]=x[v>>4];out[i*2+1]=x[v&15];}out[64]=0;}
};
}
