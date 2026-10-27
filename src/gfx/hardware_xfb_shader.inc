// SPDX-License-Identifier: GPL-2.0-or-later
R"glsl(#version 430 core
layout(local_size_x=64) in;
layout(std430,binding=0) readonly buffer Efb { uint efb[]; };
layout(std430,binding=3) readonly buffer Parameters { uint params[]; };
layout(std430,binding=7) writeonly buffer Output { uint words[]; };

ivec3 rgb(uint x,uint y) {
  uint value=efb[y*640u+x];
  if (params[7]!=1u) return ivec3((value>>16u)&255u,(value>>8u)&255u,value&255u);
  ivec3 result=ivec3((value>>18u)&63u,(value>>12u)&63u,(value>>6u)&63u);
  return (result<<2)|(result>>4);
}
int rounded(int value) { return (value>>8)+((value>>7)&1); }
ivec3 yuv(uint x,uint y) {
  uint previous=y!=0u ? max(params[8]!=0u ? params[3] : 0u,y-1u) : 0u;
  uint next=min(params[9]!=0u ? params[3]+params[4]-1u : 527u,y+1u);
  ivec3 sum=rgb(x,previous)*int(params[10])+rgb(x,y)*int(params[11])+rgb(x,next)*int(params[12]);
  ivec3 color=min(ivec3(255),sum>>6);
  color=ivec3(params[32u+uint(color.r)],params[32u+uint(color.g)],params[32u+uint(color.b)]);
  return ivec3(rounded(66*color.r+129*color.g+25*color.b),
               rounded(-38*color.r-74*color.g+112*color.b),
               rounded(112*color.r-94*color.g-18*color.b));
}
void main() {
  uint index=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*params[23];
  if (index>=params[0]) return;
  uint row=index/params[2],word=index%params[2];
  uint encoded=row*params[1]+word*4u;
  uint pixel=encoded/2u;
  uint x=pixel%params[5];
  precise double position=double(pixel/params[5])*packDouble2x32(uvec2(params[13],params[14]));
  double lower=floor(position);
  uint source_row=uint(lower)+(position-lower>=0.5 ? 1u : 0u);
  uint y=params[3]+source_row;
  ivec3 before=yuv(x!=0u ? x-1u : 0u,y),first=yuv(x,y),second=yuv(x+1u,y);
  uint a=uint(first.x+16)&255u;
  uint b=uint(128+((before.y+2*first.y+second.y)>>2))&255u;
  uint c=uint(second.x+16)&255u;
  uint d=uint(128+((before.z+2*first.z+second.z)>>2))&255u;
  words[index]=a|(b<<8u)|(c<<16u)|(d<<24u);
}
)glsl"
