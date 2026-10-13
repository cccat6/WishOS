// E6 presentation adapter. The UI engine emits ordinary RGB565 regions;
// palette mapping and controller addressing belong to this display backend.
static uint8_t inkFrom565(uint16_t v){int r=(v>>11)*255/31,g=((v>>5)&63)*255/63,b=(v&31)*255/31;const int colors[6][4]={{0,0,0,0},{255,255,255,1},{255,255,0,2},{255,0,0,3},{0,0,255,5},{0,255,0,6}};int best=INT32_MAX;uint8_t code=0;for(auto&c:colors){int d=(r-c[0])*(r-c[0])+(g-c[1])*(g-c[1])+(b-c[2])*(b-c[2]);if(d<best){best=d;code=c[3];}}return code;}

static bool renderUiBand(int x,nm::Stripe&stripe){
 stripe.startColumns(x,1);
 UiSink sink{&stripe,[](void*ctx,const lv_area_t&a,const uint16_t*pixels){
   auto&band=*static_cast<nm::Stripe*>(ctx);int width=a.x2-a.x1+1;
   int left=max(int(a.x1),band.top),right=min(int(a.x2),band.top+15);
   for(int y=a.y1;y<=a.y2;++y)for(int col=left;col<=right;++col)
     band.drawPixel(col,y,inkFrom565(pixels[(y-a.y1)*width+col-a.x1]));
 }};
 lv_area_t area{x,0,x+15,board::HEIGHT-1};return renderUiRegion(area,sink);
}
