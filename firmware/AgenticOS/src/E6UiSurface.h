// E6 presentation adapter. The UI engine emits ordinary RGB565 regions;
// palette mapping and controller addressing belong to this display backend.
static uint8_t inkFrom565(uint16_t v){int r=(v>>11)*255/31,g=((v>>5)&63)*255/63,b=(v&31)*255/31;const int colors[6][4]={{0,0,0,0},{255,255,255,1},{255,255,0,2},{255,0,0,3},{0,0,255,5},{0,255,0,6}};int best=INT32_MAX;uint8_t code=0;for(auto&c:colors){int d=(r-c[0])*(r-c[0])+(g-c[1])*(g-c[1])+(b-c[2])*(b-c[2]);if(d<best){best=d;code=c[3];}}return code;}

static bool renderUiBand(int x,nm::Stripe&stripe){
 stripe.startColumns(x,1);
 UiSink sink{&stripe,[](void*ctx,const lv_area_t&a,const uint16_t*pixels){
   auto&band=*static_cast<nm::Stripe*>(ctx);int width=a.x2-a.x1+1;
   for(int y=a.y1;y<=a.y2;++y)for(int x=a.x1;x<=a.x2;++x){int px=x,py=y;
    switch(uiRenderedView.rotation){case 90:px=board::WIDTH-1-y;py=x;break;case 180:px=board::WIDTH-1-x;py=board::HEIGHT-1-y;break;case 270:px=y;py=board::HEIGHT-1-x;break;}
    if(px>=band.top&&px<band.top+16)band.drawPixel(px,py,inkFrom565(pixels[(y-a.y1)*width+x-a.x1]));
   }
 }};
 lv_area_t area{x,0,x+15,board::HEIGHT-1};
 switch(uiRenderedView.rotation){case 90:area={0,board::WIDTH-16-x,board::HEIGHT-1,board::WIDTH-1-x};break;case 180:area={board::WIDTH-16-x,0,board::WIDTH-1-x,board::HEIGHT-1};break;case 270:area={0,x,board::HEIGHT-1,x+15};break;}
 return renderUiRegion(area,sink);
}
