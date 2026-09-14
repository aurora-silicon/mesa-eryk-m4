/* Offline G16G SIMD broadcast candidates; require exact re-encoding.
 * Derive register-field masks from the composer rather than guessed bit shifts.
 * Candidate scanning alone is not proof of instruction boundaries or execution.
 */
#include "asahi/compiler/agx3_subgroup_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int pack(struct agx3_subgroup_shuffle_desc *d,unsigned char *out) {
 uint8_t size;
 return agx3_subgroup_encode_shuffle(AGX_ISA_ENCODING_AGX3B,d,out,&size)&&size==10;
}
int main(int argc,char **argv) {
 if(argc!=2){fprintf(stderr,"Usage: %s TEXT.bin\n",argv[0]);return 2;}
 FILE *f=fopen(argv[1],"rb");if(!f){perror(argv[1]);return 2;}
 if(fseek(f,0,SEEK_END))return 2;
 long n=ftell(f);if(n<0||n>8*1024*1024)return 2;rewind(f);
 unsigned char *data=malloc(n?n:1);if(!data||fread(data,1,n,f)!=(size_t)n)return 2;fclose(f);
 struct agx3_subgroup_shuffle_desc base={.lane_kind=AGX3_SUBGROUP_LANE_REGISTER};
 unsigned char zero[16],out[16],mask[3][10]={{0}};
 if(!pack(&base,zero))return 2;
 for(unsigned field=0;field<3;field++)for(unsigned v=0;v<192;v+=2){
  struct agx3_subgroup_shuffle_desc d=base;
  *(field==0?&d.dest:field==1?&d.src:&d.lane)=v;
  if(!pack(&d,out))return 2;
  for(unsigned j=0;j<10;j++)mask[field][j]|=out[j]^zero[j];
 }
 for(long offset=0;offset+10<=n;offset+=2){
  if((data[offset]|((unsigned)data[offset+1]<<8))%2048!=0x447)continue;
  struct agx3_subgroup_shuffle_desc d=base;int valid=1;
  for(unsigned field=0;field<3;field++){
   int found=0;
   for(unsigned v=0;v<192;v+=2){
    struct agx3_subgroup_shuffle_desc t=base;
    *(field==0?&t.dest:field==1?&t.src:&t.lane)=v;
    pack(&t,out);int match=1;
    for(unsigned j=0;j<10;j++)if((out[j]^data[offset+j])&mask[field][j])match=0;
    if(match){*(field==0?&d.dest:field==1?&d.src:&d.lane)=v;found=1;break;}
   }
   if(!found){valid=0;break;}
  }
  if(!valid)continue;
  for(unsigned hints=0;hints<8;hints++){
   d.dest_hint=hints&1;d.src_live=hints&2;d.lane_live=hints&4;
   if(!pack(&d,out)||memcmp(out,data+offset,10))continue;
   printf("offset=%#lx dest=%u src=%u lane=%u dest_hint=%u src_live=%u lane_live=%u bytes=",offset,d.dest,d.src,d.lane,d.dest_hint,d.src_live,d.lane_live);
   for(unsigned j=0;j<10;j++)printf("%02x",out[j]);puts("");
  }
 }
 free(data);return 0;
}
