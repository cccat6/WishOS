// Six-color host raster fixture. No controller commands or waveform data.
static uint8_t fixtureInk(uint16_t value){
 static constexpr uint8_t colors[][3]={{0,0,0},{255,255,255},{255,255,0},{255,0,0},{0,0,255},{0,255,0}},codes[]={0,1,2,3,5,6};
 int r=(value>>11)*255/31,g=((value>>5)&63)*255/63,b=(value&31)*255/31,best=INT32_MAX;uint8_t result=0;
 for(unsigned i=0;i<6;++i){int dr=r-colors[i][0],dg=g-colors[i][1],db=b-colors[i][2],distance=dr*dr+dg*dg+db*db;if(distance<best){best=distance;result=codes[i];}}return result;
}
// The UI renderer emits RGB565. This adapter maps into the selected display
// palette and plane; applications never encode waveform stages.
static bool renderUiBand(int x,nm::Stripe&stripe){
 stripe.startColumns(x,1);
 struct Context{nm::Stripe*stripe;} context{&stripe};
 UiSink sink{&context,[](void*ctx,const lv_area_t&a,const uint16_t*pixels){
   auto&c=*static_cast<Context*>(ctx);auto&band=*c.stripe;int width=a.x2-a.x1+1;
   for(int y=a.y1;y<=a.y2;++y)for(int x=a.x1;x<=a.x2;++x){int px=x,py=y;
    switch(uiRenderedView.rotation){case 90:px=board::WIDTH-1-y;py=x;break;case 180:px=board::WIDTH-1-x;py=board::HEIGHT-1-y;break;case 270:px=y;py=board::HEIGHT-1-x;break;}
    if(px>=band.top&&px<band.top+16)band.drawPixel(px,py,fixtureInk(pixels[(y-a.y1)*width+x-a.x1]));
   }
 }};
 lv_area_t area{x,0,x+15,board::HEIGHT-1};
 switch(uiRenderedView.rotation){case 90:area={0,board::WIDTH-16-x,board::HEIGHT-1,board::WIDTH-1-x};break;case 180:area={board::WIDTH-16-x,0,board::WIDTH-1-x,board::HEIGHT-1};break;case 270:area={0,x,board::HEIGHT-1,x+15};break;}
 return renderUiRegion(area,sink);
}
