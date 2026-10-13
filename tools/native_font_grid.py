"""Align source stems to the native square-pixel grid before rasterization."""
from functools import lru_cache
import math
import numpy as np
from fontTools.ttLib import TTFont
from fontTools.pens.basePen import BasePen

class FlatPen(BasePen):
    def __init__(self,glyphset):super().__init__(glyphset);self.contours=[];self.points=[]
    def _moveTo(self,p):self.points=[p]
    def _lineTo(self,p):self.points.append(p)
    def _qCurveToOne(self,control,end):
        start=self._getCurrentPoint()
        n=max(4,min(64,math.ceil((math.dist(start,control)+math.dist(control,end))/20)))
        for k in range(1,n+1):
            t=k/n;self.points.append(tuple((1-t)**2*start[i]+2*(1-t)*t*control[i]+t*t*end[i] for i in (0,1)))
    def _curveToOne(self,c1,c2,end):
        start=self._getCurrentPoint()
        for k in range(1,33):
            t=k/32;self.points.append(tuple((1-t)**3*start[i]+3*(1-t)**2*t*c1[i]+3*(1-t)*t*t*c2[i]+t**3*end[i] for i in (0,1)))
    def _closePath(self):self.contours.append(self.points);self.points=[]
    def _endPath(self):self._closePath()

def inside(contours,x,y):
    winding=0
    for points in contours:
        for a,b in zip(points,points[1:]+points[:1]):
            cross=(b[0]-a[0])*(y-a[1])-(x-a[0])*(b[1]-a[1])
            if a[1]<=y<b[1] and cross>0:winding+=1
            elif b[1]<=y<a[1] and cross<0:winding-=1
    return winding!=0

def edges(coords,ends,flags,axis,units):
    result=[];start=0;other=1-axis
    for end in ends:
        indices=list(range(start,end+1));start=end+1
        for ia,ib in zip(indices,indices[1:]+indices[:1]):
            a,b=coords[ia],coords[ib]
            if not(flags[ia]&1 and flags[ib]&1):continue
            if abs(a[axis]-b[axis])>units*.001 or abs(a[other]-b[other])<units*.08:continue
            result.append(((a[axis]+b[axis])/2,min(a[other],b[other]),max(a[other],b[other]),1 if b[other]>a[other] else -1))
    return result

def find_stems(coords,ends,flags,contours,axis,units):
    candidates=[];lines=edges(coords,ends,flags,axis,units)
    for i,a in enumerate(lines):
        for b in lines[i+1:]:
            width=abs(a[0]-b[0]);lo=max(a[1],b[1]);hi=min(a[2],b[2])
            if a[3]==b[3] or not units*.02<=width<=units*.13 or hi-lo<units*.08:continue
            at=(a[0]+b[0])/2
            samples=[lo+(hi-lo)*f for f in (.25,.5,.75)]
            if not all(inside(contours,at,p) if axis==0 else inside(contours,p,at) for p in samples):continue
            candidates.append((min(a[0],b[0]),max(a[0],b[0]),hi-lo))
    return candidates

def align_axis(values,stems,standard,size,units,phase=0):
    anchors={};accepted=0;rejected=0
    for low,high,length in sorted(stems,key=lambda stem:-stem[2]):
        a=low*size/units+phase;b=high*size/units+phase;width=b-a
        target=max(1,math.floor((standard if .75*standard<=high-low<=1.3*standard else high-low)*size/units+.5))
        left=math.floor((a+b-target)/2+.5);right=left+target
        if max(abs(left-a),abs(right-b))>.75:rejected+=1;continue
        proposed=dict(anchors);valid=True
        for old,new in ((a,left),(b,right)):
            if old in proposed and abs(proposed[old]-new)>.01:valid=False;break
            proposed[old]=new
        ordered=sorted(proposed)
        for x,y in zip(ordered,ordered[1:]):
            gap=proposed[y]-proposed[x]
            if gap<=0 or (y-x>=.75 and gap<1):valid=False;break
        if not valid:rejected+=1;continue
        anchors=proposed;accepted+=1
    old=sorted(anchors);shifted=np.asarray(values,dtype=float)*size/units+phase
    if old:
        new=[anchors[x] for x in old];out=np.interp(shifted,old,new)
        before=shifted<old[0];after=shifted>old[-1]
        out[before]=shifted[before]+new[0]-old[0];out[after]=shifted[after]+new[-1]-old[-1]
    else:out=shifted.copy()
    return out-phase,{'stems':len(stems),'aligned':accepted,'rejected':rejected,'max_move':round(float(np.abs(out-shifted).max()) if len(out) else 0,4)}

class OutlineGrid:
    def __init__(self,path):
        self.font=TTFont(path);self.units=self.font['head'].unitsPerEm;self.cmap=self.font.getBestCmap();self.glyphset=self.font.getGlyphSet()
        self.standards=[]
        for axis in (0,1):
            stems=[stem for c in '日口田目中木林上下十王工' for stem in self.geometry(c)[3][axis]]
            widths=np.array([b-a for a,b,_ in stems]);weights=np.array([length for _,_,length in stems])
            scores=[weights[np.abs(widths-w)<=self.units*.008].sum() for w in widths]
            peak=widths[int(np.argmax(scores))];selected=widths[np.abs(widths-peak)<=self.units*.008]
            self.standards.append(float(np.median(selected)))

    @lru_cache(maxsize=512)
    def geometry(self,char):
        if ord(char) not in self.cmap:raise ValueError('font lacks character '+char)
        name=self.cmap[ord(char)];g=self.font['glyf'][name]
        coords,ends,flags=g.getCoordinates(self.font['glyf'])
        pen=FlatPen(self.glyphset);self.glyphset[name].draw(pen)
        stems=[find_stems(coords,ends,flags,pen.contours,axis,self.units) for axis in (0,1)]
        return np.asarray(coords,dtype=float).reshape(-1,2),list(ends),list(flags),stems

    def glyph_points(self,char,size,grid=True,phase=0):
        coords,ends,flags,stems=self.geometry(char);points=coords*size/self.units;stats=[]
        if grid and len(points):
            points[:,0],xstats=align_axis(coords[:,0],stems[0],self.standards[0],size,self.units,phase)
            ys=[(-b,-a,length) for a,b,length in stems[1]]
            negative_y,ystats=align_axis(-coords[:,1],ys,self.standards[1],size,self.units)
            points[:,1]=negative_y;stats=[xstats,ystats]
        else:points[:,1]*=-1
        return points,ends,flags,stats
