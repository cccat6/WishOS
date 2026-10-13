"""FreeType native monochrome glyph compiler with source stem alignment.

No glyph postprocessing, supersampling, or synthesized font weight. Horizontal
Chinese/Latin only, as for native_text. Caches are local to a host process.
"""
from functools import lru_cache
import math
from PIL import Image
import freetype

import numpy as np
NEIGHBORS=((-1,-1),(-1,0),(-1,1),(0,-1),(0,1),(1,-1),(1,0),(1,1))
def components(mask, diagonal=True):
    """Label source support; source components delimit area accounting."""
    h,w=mask.shape; labels=np.full((h,w),-1,dtype=np.int32); number=0
    offsets=NEIGHBORS if diagonal else ((-1,0),(0,-1),(0,1),(1,0))
    for sy,sx in np.argwhere(mask):
        if labels[sy,sx]>=0:continue
        stack=[(int(sy),int(sx))];labels[sy,sx]=number
        while stack:
            y,x=stack.pop()
            for dy,dx in offsets:
                yy,xx=y+dy,x+dx
                if 0<=yy<h and 0<=xx<w and mask[yy,xx] and labels[yy,xx]<0:
                    labels[yy,xx]=number;stack.append((yy,xx))
        number+=1
    return labels,number



@lru_cache(maxsize=3)
def outline_model(path):
    from native_font_grid import OutlineGrid
    return OutlineGrid(path)

def bitmap_record(g,advance):
    b=g.bitmap
    if b.width and b.rows:
        if b.pixel_mode!=freetype.FT_PIXEL_MODE_MONO:raise ValueError('expected monochrome glyph')
        image=Image.frombytes('1',(b.width,b.rows),bytes(b.buffer),'raw','1',abs(b.pitch),1 if b.pitch>=0 else -1)
    else:image=None
    return image,int(g.bitmap_left),-int(g.bitmap_top),advance

def signature(image):
    import numpy as np
    if image is None:return (0,0)
    ink=np.pad(np.asarray(image,dtype=bool),1)
    return components(ink,True)[1],components(~ink,False)[1]-1

@lru_cache(maxsize=3)
def reference_face(path):
    face=freetype.Face(str(path));face.set_pixel_sizes(0,128)
    return face

@lru_cache(maxsize=4096)
def reference_signature(path,char):
    face=reference_face(path)
    face.load_char(char,freetype.FT_LOAD_NO_HINTING|freetype.FT_LOAD_NO_AUTOHINT|freetype.FT_LOAD_NO_BITMAP)
    face.glyph.render(freetype.FT_RENDER_MODE_MONO)
    return signature(bitmap_record(face.glyph,0)[0])

class StemFont:
    """Align source outlines, preserving raw mono components/counters on fallback."""
    def __init__(self,path,size,grid=True):
        self.face=freetype.Face(str(path));self.face.set_pixel_sizes(0,size)
        self.path=str(path);self.model=outline_model(path);self.size=size;self.grid=grid

    @lru_cache(maxsize=2048)
    def glyph(self,char):
        import numpy as np
        flags=freetype.FT_LOAD_NO_HINTING|freetype.FT_LOAD_NO_AUTOHINT|freetype.FT_LOAD_NO_BITMAP
        self.face.load_char(char,flags);g=self.face.glyph
        old=np.array([(p[0]/64,-p[1]/64) for p in g.outline.points],dtype=float).reshape(-1,2)
        points,_,_,stats=self.model.glyph_points(char,self.size,self.grid)
        expected=self.model.glyph_points(char,self.size,False)[0]
        compatible=len(old)==len(expected) and (not len(old) or np.max(np.abs(old-expected))<=.05)
        advance=math.floor(g.advance.x/64+.5)
        g.render(freetype.FT_RENDER_MODE_MONO);original=bitmap_record(g,advance)
        changed=bool(self.grid and compatible and len(old) and np.any(np.abs(old-points)>.02))
        if not changed:return original
        # Reload for each trial; never write through a released outline.
        candidates=[('xy',points,stats)]
        xonly=points.copy();xonly[:,1]=old[:,1]
        yonly=points.copy();yonly[:,0]=old[:,0]
        candidates.extend([('x',xonly,stats[:1]),('y',yonly,stats[1:])])
        original_signature=signature(original[0])
        for axes,trial,axis_stats in candidates:
            if not np.any(np.abs(old-trial)>.02):continue
            self.face.load_char(char,flags);g=self.face.glyph;vectors=g.outline._FT_Outline.points
            for i,(x,y) in enumerate(trial):
                vectors[i].x=round(x*64);vectors[i].y=round(-y*64)
            g.render(freetype.FT_RENDER_MODE_MONO);aligned=bitmap_record(g,advance)
            current_signature=signature(aligned[0])
            if current_signature!=original_signature and current_signature!=reference_signature(self.path,char):continue
            return aligned
        return original
