#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int LLVMFuzzerTestOneInput(const uint8_t* d, size_t n);
static void put32(uint8_t*p,uint32_t v){memcpy(p,&v,4);}
int main(int argc,char**argv){
  int mode=atoi(argv[1]); uint8_t m[400]; memset(m,0,sizeof m);
  if(mode==1){ /* superenum: descOff wraps */ m[0]=0; put32(m+1+24,0xFFFFFFF0u); LLVMFuzzerTestOneInput(m,1+100); }
  if(mode==2){ /* superenum: packedOff negative */ m[0]=0; put32(m+1+8,1); put32(m+1+16,0x80000000u); LLVMFuzzerTestOneInput(m,1+100); }
  if(mode==3){ /* superenum: size jumps forward, getCoded reads far */ m[0]=0; put32(m+1+8,1); put32(m+1+16,36); put32(m+1+36,0x10000000u); put32(m+1+36+12,0x4); LLVMFuzzerTestOneInput(m,1+100); }
  if(mode==4){ /* createplayer: snLen+lnLen wrap -> o negative */ m[0]=1; uint8_t*pp=m+1+28; put32(pp+8,5); put32(pp+12,0); put32(pp+16,0xF0000000u); put32(pp+20,32); LLVMFuzzerTestOneInput(m,1+120); }
  if(mode==5){ /* forward: spOff negative -> write */ m[0]=2; uint8_t*pp=m+1+20+28; put32(pp+8,5); put32(pp+12,0); put32(pp+16,0xFFFFFF00u); put32(pp+20,32); LLVMFuzzerTestOneInput(m,1+200); }
  if(mode==7){ uint8_t*pp=m+20+28; put32(pp+12,0); put32(pp+16,0xFFFFFED8u); put32(pp+20,32); extern int fwd_only(uint8_t*,int); fwd_only(m,200);} if(mode==6){ /* random */ srand(1); for(int it=0;it<2000000;it++){ int n=2+rand()%300; uint8_t b[302]; for(int i=0;i<n;i++) b[i]=(rand()%4==0)?0xFF:rand(); LLVMFuzzerTestOneInput(b,n);} puts("random done"); }
  puts("ok"); return 0; }
