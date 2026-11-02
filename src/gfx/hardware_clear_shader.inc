// SPDX-License-Identifier: GPL-2.0-or-later
R"glsl(#version 430 core
layout(local_size_x=8,local_size_y=8) in;
layout(std430,binding=0) buffer Efb { uint efb[]; };
layout(std430,binding=3) readonly buffer Parameters { uint params[]; };
void main() {
  uvec2 pixel=gl_GlobalInvocationID.xy+uvec2(params[15],params[16]);
  if (pixel.x>=params[17] || pixel.y>=params[18]) return;
  uint offset=pixel.y*640u+pixel.x;
  uint previous=efb[offset],clear=params[19];
  uint mask=params[20];
  if (params[7]==1u) {
    uvec4 value=uvec4((previous>>18u)&63u,(previous>>12u)&63u,(previous>>6u)&63u,previous&63u);
    value=(value<<2u)|(value>>4u);
    uvec4 replacement=uvec4(clear>>24u,(clear>>16u)&255u,(clear>>8u)&255u,clear&255u);
    if ((mask&8u)!=0u) value.rgb=replacement.rgb;
    if ((mask&16u)!=0u) value.a=replacement.a;
    value>>=2u;
    efb[offset]=(value.r<<18u)|(value.g<<12u)|(value.b<<6u)|value.a;
  } else {
    uint color_mask=(mask&8u)!=0u ? 16777215u : 0u;
    efb[offset]=((clear>>8u)&color_mask)|(previous&~color_mask&16777215u);
  }
  if (params[21]!=0u) efb[337920u+offset]=params[22];
}
)glsl"
