#!/usr/bin/env python3
"""Compile PNG/JPEG or SVG into LVGL's uncompressed, streamable RGB565(A8) file.
Runs on the host. The MCU reads image regions instead of decoding a complete PNG.
"""
from __future__ import annotations
import argparse, json, struct, subprocess
from pathlib import Path
from PIL import Image, ImageOps

def compile_image(source: Path, destination: Path, width=None, height=None, fit="contain"):
    if fit not in ('contain','cover','stretch'):raise ValueError('Unsupported fit')
    if (width is not None and width<=0) or (height is not None and height<=0):raise ValueError('Dimensions must be positive')
    if source.suffix.lower()=='.svg':
        module=Path(__file__).with_name('render_os_svg.mjs')
        result=subprocess.run(['node',str(module),str(source.resolve()),str(width or 0),str(height or 0)],check=True,stdout=subprocess.PIPE)
        w,h=struct.unpack('<II',result.stdout[:8]);image=Image.frombytes('RGBA',(w,h),result.stdout[8:])
    else:
        image=ImageOps.exif_transpose(Image.open(source)).convert('RGBA')
    if width or height:
        w=width or round(image.width*height/image.height);h=height or round(image.height*width/image.width)
        if fit=='cover':image=ImageOps.fit(image,(w,h),Image.Resampling.LANCZOS)
        elif fit=='stretch':image=image.resize((w,h),Image.Resampling.LANCZOS)
        else:
            resized=ImageOps.contain(image,(w,h),Image.Resampling.LANCZOS)
            image=Image.new('RGBA',(w,h),(0,0,0,0));image.alpha_composite(resized,((w-resized.width)//2,(h-resized.height)//2))
    w,h=image.size
    if not 0<w<=16383 or not 0<h<=65535:raise ValueError('Dimensions exceed LVGL header/stride range')
    pixels=image.tobytes();alpha=any(pixels[i]!=255 for i in range(3,len(pixels),4))
    destination.parent.mkdir(parents=True,exist_ok=True)
    with destination.open('wb') as out:
        out.write(struct.pack('<BBHHHHH',0x19,0x14 if alpha else 0x12,0,w,h,w*2,0))
        for y in range(h):
            row=bytearray(w*2)
            for x in range(w):
                at=(y*w+x)*4;r,g,b=pixels[at:at+3]
                struct.pack_into('<H',row,x*2,((r>>3)<<11)|((g>>2)<<5)|(b>>3))
            out.write(row)
        if alpha:out.write(pixels[3::4])
    return {'file':str(destination),'width':w,'height':h,'format':'RGB565A8' if alpha else 'RGB565','bytes':destination.stat().st_size,'compressed':False,'streamable':True}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('output',type=Path);p.add_argument('--width',type=int);p.add_argument('--height',type=int);p.add_argument('--fit',choices=['contain','cover','stretch'],default='contain')
    a=p.parse_args();print(json.dumps(compile_image(a.source,a.output,a.width,a.height,a.fit),ensure_ascii=False))
if __name__=='__main__':main()
