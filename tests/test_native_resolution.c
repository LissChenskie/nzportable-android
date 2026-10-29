#include "../android/native/render_math.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
 int w,h;
 nzp_scale_size(2400,1080,100,&w,&h);assert(w==2400&&h==1080);
 nzp_scale_size(2400,1080,90,&w,&h);assert(w==2160&&h==972);
 nzp_scale_size(2400,1080,80,&w,&h);assert(w==1920&&h==864);
 nzp_scale_size(2400,1080,75,&w,&h);assert(w==1800&&h==810);
 nzp_scale_size(2400,1080,60,&w,&h);assert(w==1440&&h==648);
 nzp_scale_size(1281,733,100,&w,&h);assert(w==1281&&h==733);
 nzp_scale_size(1281,733,110,&w,&h);assert(w==1281&&h==733);
 int ui=nzp_ui_scale(1080,100);assert(ui==3);
 assert(nzp_ui_scale(1080,125)==4);
 assert(nzp_ui_scale(1080,75)==2);
 assert(nzp_ui_scale(432,100)==1);
 printf("native render size and independent UI scale OK\n");
 return 0;
}
