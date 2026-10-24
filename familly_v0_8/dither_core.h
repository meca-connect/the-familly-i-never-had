#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <limits>

namespace family {
inline uint32_t apiSeed(uint32_t value){return value&0x7fffffffU;} // Provider-compatible signed-32-bit seed.
// Same palette and double-precision serpentine diffusion as selected Python test C.
static constexpr uint8_t colors[6][3]={{0,0,0},{255,255,255},{255,243,56},{191,0,0},{100,64,255},{67,138,28}};
inline bool floydSteinberg(const uint8_t* rgb,size_t length,uint8_t* packed,size_t capacity) {
  constexpr int width=400,height=600;
  if (!rgb||!packed||length!=size_t(width*height*3)||capacity<size_t(width*height/2)) return false;
  double* a=static_cast<double*>(calloc((width+2)*3,sizeof(double)));
  double* b=static_cast<double*>(calloc((width+2)*3,sizeof(double)));
  if (!a||!b) { free(a);free(b);return false; }
  memset(packed,0,width*height/2);
  for(int y=0;y<height;++y) {
#ifdef ARDUINO
    if(y%8==0) delay(1); // Keep the radio/idle watchdog responsive during diffusion.
#endif
    memset(b,0,(width+2)*3*sizeof(double));
    const int direction=(y%2==0)?1:-1;
    for(int step=0;step<width;++step) {
      const int x=direction==1?step:width-1-step;
      double value[3];
      for(int c=0;c<3;++c) value[c]=std::min(255.0,std::max(0.0,double(rgb[(y*width+x)*3+c])+a[(x+1)*3+c]));
      int best=0;double minDistance=std::numeric_limits<double>::infinity();
      for(int i=0;i<6;++i) {
        double distance=0;
        for(int c=0;c<3;++c) {double d=value[c]-colors[i][c];distance+=d*d;}
        if(distance<minDistance){minDistance=distance;best=i;}
      }
      const size_t offset=y*width+x;
      if(offset&1) packed[offset>>1]|=best; else packed[offset>>1]|=best<<4;
      for(int c=0;c<3;++c) {
        const double error=value[c]-colors[best][c];
        a[(x+1+direction)*3+c]+=error*7/16;
        b[(x+1-direction)*3+c]+=error*3/16;
        b[(x+1)*3+c]+=error*5/16;
        b[(x+1+direction)*3+c]+=error/16;
      }
    }
    std::swap(a,b);
  }
  free(a);free(b);return true;
}
}
