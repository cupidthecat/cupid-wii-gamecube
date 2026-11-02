// SPDX-License-Identifier: GPL-2.0-or-later
R"glsl(#version 430 core
layout(local_size_x=64) in;
layout(std430,binding=0) readonly buffer Efb { uint efb[]; };
layout(std430,binding=3) readonly buffer Parameters { uint params[]; };
layout(std430,binding=7) writeonly buffer Output { uint words[]; };

uvec4 sample_pixel(uint pixel,uint block) {
  uint x=params[15]+((block%params[1])*params[3]+pixel%params[3])*params[5];
  uint y=params[16]+((block/params[1])*params[4]+pixel/params[3])*params[5];
  uvec4 sum=uvec4(0u);
  for(uint dy=0u;dy<params[5];++dy)
    for(uint dx=0u;dx<params[5];++dx) {
      uint position=(y+dy)*640u+x+dx;
      if(params[7]==1u) {
        uint value=efb[position];
        sum+=uvec4((value>>18u)&63u,(value>>12u)&63u,(value>>6u)&63u,value&63u);
      } else {
        uint value=efb[position+(params[7]==3u ? 337920u : 0u)];
        sum+=uvec4((value>>16u)&255u,(value>>8u)&255u,value&255u,255u);
      }
    }
  if(params[7]==1u)
    return params[5]==2u ? sum+(sum>>6u) : (sum<<2u)|(sum>>4u);
  if(params[5]==2u) sum>>=2u;
  sum.a=255u;
  return sum;
}

uint encoded_byte(uint offset,uint block) {
  uint format=params[6];
  if(format==0u) {
    uvec4 first=sample_pixel(offset*2u,block),second=sample_pixel(offset*2u+1u,block);
    uint a=first.r,b=second.r;
    if(params[8]!=0u) {
      a=(4096u+66u*first.r+129u*first.g+25u*first.b)>>8u;
      b=(4096u+66u*second.r+129u*second.g+25u*second.b)>>8u;
    }
    return (a&240u)|(b>>4u);
  }
  uint pixel=format==6u ? (offset&31u)/2u :
             (format==3u || format==4u || format==5u || format>=11u ? offset/2u : offset);
  uvec4 color=sample_pixel(pixel,block);
  uint red=color.r;
  if(params[8]!=0u) red=(4096u+66u*color.r+129u*color.g+25u*color.b)>>8u;
  bool second=(offset&1u)!=0u;
  uint value=0u;
  switch(format) {
  case 1u:case 8u:return red;
  case 2u:return (color.a&240u)|(red>>4u);
  case 3u:return second ? red : color.a;
  case 4u:value=((red>>3u)<<11u)|((color.g>>2u)<<5u)|(color.b>>3u);break;
  case 5u:value=color.a>=224u ? 32768u|((red>>3u)<<10u)|((color.g>>3u)<<5u)|(color.b>>3u) :
                           ((color.a>>5u)<<12u)|((red>>4u)<<8u)|((color.g>>4u)<<4u)|(color.b>>4u);break;
  case 6u:
    if(offset<32u) return second ? (params[9]!=0u ? color.b : red) : color.a;
    return second ? (params[9]!=0u ? red : color.b) : color.g;
  case 7u:return color.a;
  case 9u:return color.g;
  case 10u:return color.b;
  case 11u:return second ? (params[9]!=0u ? color.b : red) : color.g;
  case 12u:return second ? color.g : (params[9]!=0u ? red : color.b);
  }
  return second ? value&255u : value>>8u;
}

void main() {
  uint index=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*params[23];
  if(index>=params[0]) return;
  uint bytes=index*4u,block=bytes/params[2],offset=bytes%params[2];
  uint result=0u;
  for(uint byte=0u;byte<4u;++byte)
    result|=(encoded_byte(offset+byte,block)&255u)<<(byte*8u);
  words[index]=result;
}
)glsl"
