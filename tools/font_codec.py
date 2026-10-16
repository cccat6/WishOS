"""Deterministic lossless Huffman packing of independently decodable glyph payloads."""
import collections
import heapq


def pack_glyphs(blob,glyphs):
    counts=collections.Counter(blob)
    if len(counts)<2:raise ValueError("Native font requires at least two payload symbols")
    queue=[(count,byte,byte) for byte,count in counts.items()];heapq.heapify(queue);serial=256
    while len(queue)>1:
        a,_,left=heapq.heappop(queue);b,_,right=heapq.heappop(queue)
        heapq.heappush(queue,(a+b,serial,(left,right)));serial+=1
    tree=[];codes={}
    def visit(node,bits=()):
        if isinstance(node,int):codes[node]=bits;return 256+node
        index=len(tree);tree.append(None);tree[index]=(visit(node[0],bits+(0,)),visit(node[1],bits+(1,)));return index
    visit(queue[0][2]);packed=bytearray();records=[]
    for glyph in glyphs:
        row=list(glyph);stored=row[-2]&32767;source=blob[row[-1]:row[-1]+stored];row[-1]=len(packed);byte=used=0
        for symbol in source:
            for bit in codes[symbol]:
                byte=(byte<<1)|bit;used+=1
                if used==8:packed.append(byte);byte=used=0
        if used:packed.append(byte<<(8-used))
        records.append(tuple(row))
    return packed,records,tree


def header(blob,glyphs):
    blob,glyphs,tree=pack_glyphs(blob,glyphs)
    sizes=sorted({g[1] for g in glyphs})
    if len(sizes)>8 or any(not(0<=g[0]<2**21 and 0<=g[2]<128 and 0<=g[3]<128 and -64<=g[4]<64 and -128<=g[5]<128 and 0<=g[6]<128 and 0<=g[-1]<2**20) for g in glyphs):raise ValueError('Native glyph index field range')
    declaration="""struct __attribute__((packed)) NativeGlyph {
 uint32_t code:21,size_index:3,w:7,h:7;int32_t x:7,y:8;uint32_t advance:7,stored:16,offset:20;
 constexpr NativeGlyph(uint32_t c,uint8_t s,uint8_t a,uint8_t b,int8_t d,int8_t e,uint8_t f,uint16_t g,uint32_t h):code(c),size_index(index(s)),w(a),h(b),x(d),y(e),advance(f),stored(g),offset(h){}
 static constexpr unsigned index(unsigned s){for(unsigned i=0;i<sizeof NATIVE_FONT_SIZES;++i)if(NATIVE_FONT_SIZES[i]==s)return i;return 0;}
 uint8_t fontSize()const{return NATIVE_FONT_SIZES[size_index];}
};
static_assert(sizeof(NativeGlyph)==12,"Native font index layout");"""
    lines=['#pragma once','#include <stdint.h>','// Noto Sans SC subset; SIL OFL in OFL.txt. Native square-pixel stem alignment.','// Lossless Huffman payloads and compact generated metrics; decoded pixels are unchanged.','namespace nm {','static constexpr uint8_t NATIVE_FONT_SIZES[]={'+','.join(map(str,sizes))+'};',declaration,'static const uint16_t NATIVE_FONT_TREE[][2]={']
    lines.extend('{'+str(a)+','+str(b)+'},' for a,b in tree)
    lines+=['};','static const uint8_t NATIVE_FONT_BITS[]={']
    lines.extend(','.join(map(str,blob[n:n+32]))+',' for n in range(0,len(blob),32));lines+=['};','static const NativeGlyph NATIVE_GLYPHS[]={']
    lines.extend('{'+','.join(map(str,g))+'},' for g in glyphs);lines+=['};','}']
    return '\n'.join(lines)+'\n'
