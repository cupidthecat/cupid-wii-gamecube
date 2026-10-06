// SPDX-License-Identifier: GPL-2.0-or-later
// Separate literals retain the shader bytes while meeting older compiler size limits.
R"glsl(#version 430 core
layout(local_size_x=8, local_size_y=8) in;
layout(std430, binding=0) buffer Efb { uint efb[]; };
layout(std430, binding=1) readonly buffer State { uint state[]; };
layout(std430, binding=2) coherent buffer Counters { uint counters[]; };
layout(std430, binding=3) readonly buffer Work { uint work[]; };
layout(std430, binding=4) readonly buffer Textures { uint textures[]; };
layout(std430, binding=5) buffer TextureState { uint texture_state[]; };
layout(std430, binding=6) readonly buffer FogThresholds { uint fog_thresholds[]; };

float rounded_divide(float numerator, float denominator) {
  return float(double(numerator)/double(denominator));
}

double positive_float_value(uint bits) {
  uint fraction=bits&8388607u,exponent=bits>>23u;
  if (exponent==0u) {
    if (fraction==0u) return 0.0LF;
    int leading=findMSB(fraction);
    fraction=(fraction<<uint(23-leading))&8388607u;
    exponent=uint(leading+874);
  } else exponent+=896u;
  return packDouble2x32(uvec2(fraction<<29u,(exponent<<20u)|(fraction>>3u)));
}
bool above_float_midpoint(double numerator,double denominator,uint lower) {
  precise double first=positive_float_value(lower);
  // The conceptual successor of the greatest finite binary32 value is 2^128.
  precise double second=lower==2139095039u ? packDouble2x32(uvec2(0u,1151u<<20u)) : positive_float_value(lower+1u);
  precise double midpoint=(first+second)*0.5LF;
  // Midpoint and denominator have at most 25 and 24 significand bits.
  precise double product=midpoint*denominator;
  return numerator>product || (numerator==product && (lower&1u)!=0u);
}
float fog_divide(float numerator,float denominator) {
  uint nbits=floatBitsToUint(numerator),dbits=floatBitsToUint(denominator);
  uint sign=(nbits^dbits)&2147483648u;
  nbits&=2147483647u; dbits&=2147483647u;
  if (nbits>2139095040u || dbits>2139095040u ||
      (nbits==0u && dbits==0u) || (nbits==2139095040u && dbits==2139095040u))
    return uintBitsToFloat(2143289344u);
  if (dbits==0u || nbits==2139095040u) return uintBitsToFloat(2139095040u|sign);
  if (dbits==2139095040u) return uintBitsToFloat(sign);
  if (nbits==0u) return uintBitsToFloat(sign);
  precise double n=positive_float_value(nbits),d=positive_float_value(dbits);
  uint candidate=floatBitsToUint(rounded_divide(numerator,denominator))&2147483647u;
  if (candidate>2139095040u) candidate=2139095040u;
  for (uint attempt=0u;attempt<8u;++attempt) {
    if (candidate!=0u && !above_float_midpoint(n,d,candidate-1u)) --candidate;
    else if (candidate!=2139095040u && above_float_midpoint(n,d,candidate)) ++candidate;
    else return uintBitsToFloat(candidate|sign);
  }
  // An estimate flushed to zero can be far from a nonzero subnormal quotient.
  uint lower=0u,upper=2139095040u;
  while (lower<upper) {
    uint middle=lower+(upper-lower)/2u;
    if (above_float_midpoint(n,d,middle)) lower=middle+1u;
    else upper=middle;
  }
  return uintBitsToFloat(lower|sign);
}

bool compare_value(uint mode, uint value, uint reference) {
  if (mode==0u) return false;
  if (mode==1u) return value<reference;
  if (mode==2u) return value==reference;
  if (mode==3u) return value<=reference;
  if (mode==4u) return value>reference;
  if (mode==5u) return value!=reference;
  if (mode==6u) return value>=reference;
  return true;
}
float plane(uint index, ivec2 pixel) {
  uint offset=13u+index*7u;
  precise float x=uintBitsToFloat(work[offset+3u])+float(pixel.x-int(work[offset+5u]));
  precise float y=uintBitsToFloat(work[offset+4u])+float(pixel.y-int(work[offset+6u]));
  precise float value=uintBitsToFloat(work[offset])+uintBitsToFloat(work[offset+1u])*x;
  value=value+uintBitsToFloat(work[offset+2u])*y;
  return value;
}
ivec4 unpack_rgba(uint value) {
  return ivec4((value>>24u)&255u,(value>>16u)&255u,(value>>8u)&255u,value&255u);
}
ivec4 swap_color(ivec4 value, uint table) {
  uint a=state[246u+table*2u], b=state[247u+table*2u];
  return ivec4(value[a&3u],value[(a>>2u)&3u],value[b&3u],value[(b>>2u)&3u]);
}
int konst(uint select, uint channel) {
  const int fractions[8]=int[8](255,223,191,159,128,96,64,32);
  if (select<8u) return fractions[select];
  if (select<12u) return 0;
  if (select<16u) return channel==3u ? 0 : int(state[272u+(select-12u)*4u+channel]);
  return int(state[272u+(select&3u)*4u+(select-16u)/4u]);
}
struct RegisterFile { ivec4 r0; ivec4 r1; ivec4 r2; ivec4 r3; };
ivec4 get_register(RegisterFile registers, uint index) {
  if (index==0u) return registers.r0;
  if (index==1u) return registers.r1;
  if (index==2u) return registers.r2;
  return registers.r3;
}
void set_register(inout RegisterFile registers, uint index, ivec4 value) {
  if (index==0u) registers.r0=value;
  else if (index==1u) registers.r1=value;
  else if (index==2u) registers.r2=value;
  else registers.r3=value;
}
int color_operand(uint select, uint channel, RegisterFile registers, ivec4 texture_color,
                  ivec4 raster, uint selection) {
  if (select<8u) { ivec4 value=get_register(registers,select/2u); return value[(select&1u)!=0u ? 3u : channel]; }
  if (select==8u) return texture_color[channel];
  if (select==9u) return texture_color.a;
  if (select==10u) return raster[channel];
  if (select==11u) return raster.a;
  if (select==12u) return 255;
  if (select==13u) return 128;
  if (select==14u) return konst(selection,channel);
  return 0;
}
int alpha_operand(uint select, RegisterFile registers, ivec4 texture_color,
                  ivec4 raster, uint selection) {
  if (select<4u) return get_register(registers,select).a;
  if (select==4u) return texture_color.a;
  if (select==5u) return raster.a;
  if (select==6u) return konst(selection,3u);
  return 0;
}
int combine(uint control, int a, int b, int c, int d, bool alpha) {
  uint scale=(control>>20u)&3u, bias=(control>>16u)&3u;
  bool subtract=(control&262144u)!=0u;
  int multiplier=scale==3u ? 1 : 1<<int(scale);
  int fraction=(c&255)+((c&255)>>7);
  int mix=((a&255)*(256-fraction)+(b&255)*fraction)*multiplier;
  mix+=scale==3u ? 0 : subtract ? 127 : 128;
  if (alpha && subtract) mix=(-mix)>>8;
  else { mix>>=8; if (subtract) mix=-mix; }
  int value=(d+(bias==1u ? 128 : bias==2u ? -128 : 0))*multiplier+mix;
  return scale==3u ? value>>1 : value;
}
bool combiner_compare(uint control, ivec4 a, ivec4 b, uint channel) {
  uint mode=(control>>20u)&3u, av=0u, bv=0u;
  if (mode==3u) { av=uint(a[channel]); bv=uint(b[channel]); }
  else for (uint i=0u;i<=mode;++i) { av|=uint(a[i])<<(i*8u); bv|=uint(b[i])<<(i*8u); }
  return (control&262144u)!=0u ? av==bv : av>bv;
}
uint texture_register(uint unit, uint group) {
  return state[128u+(unit&3u)+((unit&4u)<<3u)+group*4u];
}
int wrap_coord(int coordinate, int size, uint mode) {
  if (mode==1u) return coordinate&(size-1);
  if (mode==2u) { if ((coordinate&size)!=0) coordinate=~coordinate; return coordinate&(size-1); }
  return clamp(coordinate,0,size-1);
}
ivec4 fetch_texture(uint unit, ivec2 pixel, uint level) {
  uint image=texture_register(unit,2u),mode=texture_register(unit,0u);
  int width=int(((image&1023u)>>level)+1u);
  int height=int((((image>>10u)&1023u)>>level)+1u);
  pixel=ivec2(wrap_coord(pixel.x,width,mode&3u),wrap_coord(pixel.y,height,(mode>>2u)&3u));
  return unpack_rgba(textures[textures[unit*11u+level]+uint(pixel.y*width+pixel.x)]);
}
ivec4 sample_mip(uint unit, ivec2 coordinate, uint level, bool linear) {
  ivec2 scaled=(coordinate>>int(level))-(linear ? ivec2(64) : ivec2(0));
  ivec2 pixel=scaled>>7;
  if (!linear) return fetch_texture(unit,pixel,level);
  ivec2 fraction=scaled&ivec2(127);
  return (fetch_texture(unit,pixel,level)*(128-fraction.x)*(128-fraction.y)+
          fetch_texture(unit,pixel+ivec2(1,0),level)*fraction.x*(128-fraction.y)+
          fetch_texture(unit,pixel+ivec2(0,1),level)*(128-fraction.x)*fraction.y+
          fetch_texture(unit,pixel+ivec2(1,1),level)*fraction.x*fraction.y)>>14;
}
vec2 projected_uv(uint coordinate, ivec2 pixel) {
  precise float reciprocal=rounded_divide(1.0,plane(1u,pixel));
  precise float q=plane(12u+coordinate*3u,pixel)*reciprocal;
  precise float projection=q!=0.0 ? rounded_divide(reciprocal,q) : reciprocal;
  precise vec2 result=vec2(plane(10u+coordinate*3u,pixel),plane(11u+coordinate*3u,pixel))*projection;
  return result;
}
uint projected_coordinate=8u;
bool projected_derivatives_ready=false;
precise vec2 projected_value,projected_dx,projected_dy;
void prepare_projection(uint coordinate, ivec2 pixel, bool derivatives) {
  if (projected_coordinate!=coordinate) {
    projected_value=projected_uv(coordinate,pixel);
    projected_coordinate=coordinate;
    projected_derivatives_ready=false;
  }
  if (derivatives && !projected_derivatives_ready) {
    ivec2 block=pixel&~ivec2(1);
    precise vec2 origin=projected_uv(coordinate,block);
    projected_dx=projected_uv(coordinate,block+ivec2(1,0))-origin;
    projected_dy=projected_uv(coordinate,block+ivec2(0,1))-origin;
    projected_derivatives_ready=true;
  }
}
int coordinate_bits(int value) {
  return int((uint(value)&16777215u)^8388608u)-8388608;
}
ivec2 fixed_coordinates(uint coordinate, ivec2 pixel) {
  if ((state[0]&15u)==0u) return ivec2(0);
  prepare_projection(coordinate,pixel,false);
  ivec2 fixed_uv=ivec2(projected_value*128.0)&ivec2(16777215);
  return (fixed_uv^ivec2(8388608))-ivec2(8388608);
}
ivec4 sample_lod(uint unit, ivec2 fixed_uv, int lod, bool linear, uint mip_mode) {
  uint level=lod>0 && mip_mode!=0u ? uint(lod)>>4u : 0u;
  if (mip_mode==1u && (lod&15)>=8 && lod>0) ++level;
  ivec4 result=sample_mip(unit,fixed_uv,level,linear);
  if (mip_mode==2u && lod>0 && (lod&15)!=0)
    result=(result*(16-(lod&15))+sample_mip(unit,fixed_uv,level+1u,linear)*(lod&15))>>4;
  return result;
}
ivec4 sample_texture(uint unit, uint coordinate, ivec2 pixel, ivec2 fixed_uv) {
  uint mode=texture_register(unit,0u),mode1=texture_register(unit,1u);
  uint mip_mode=(mode>>5u)&3u;
  bool mag_linear=(mode&16u)!=0u,min_linear=(mode&128u)!=0u;
  uint minimum=mode1&255u,maximum=(mode1>>8u)&255u;
  // Equal filters and a fixed mip make derivatives irrelevant to the result.
  // Retain unclamped LOD selection when minification and magnification differ.
  int lod=int(minimum);
  bool linear=mag_linear;
  if (mag_linear!=min_linear || (mip_mode!=0u && minimum!=maximum)) {
    prepare_projection(coordinate,pixel,true);
    vec2 delta=(mode&256u)!=0u ? abs(projected_dx)+abs(projected_dy) :
                                max(abs(projected_dx),abs(projected_dy));
    uint bits=floatBitsToUint(max(delta.x,delta.y));
    lod=int((bits&2139095040u)>>19u)-2032+int((bits&8388607u)>>19u);
    int bias=int(((mode>>9u)&255u)^128u)-128;
    lod+=bias>>1;
    linear=lod>0 ? min_linear : mag_linear;
    if (lod>int(maximum)) lod=int(maximum);
    else if (lod<int(minimum)) lod=int(minimum);
  }
  return sample_lod(unit,fixed_uv,lod,linear,mip_mode);
}
int indirect_wrap(int value, uint mode) {
  if (mode==0u) return value;
  if (mode==6u) return 0;
  return value&((1<<(16-int(mode)))-1);
}
ivec2 indirect_coordinates(uint control, ivec2 source, ivec2 previous, ivec4 map,
                           out int alpha_bump) {
  uint format=(control>>2u)&3u, bump=(control>>7u)&3u;
  int shift=format==0u ? 0 : format==1u ? 3 : format==2u ? 4 : 5;
  ivec3 channels=ivec3(map.a,map.b,map.g);
  alpha_bump=bump==0u ? 0 : channels[bump-1u];
  alpha_bump=format==0u ? alpha_bump&248 : (alpha_bump<<(8-shift))&255;
  int bias=format==0u ? -128 : 1;
  ivec3 values=(channels>>shift)+ivec3((control&16u)!=0u ? bias : 0,
                                    (control&32u)!=0u ? bias : 0,
                                    (control&64u)!=0u ? bias : 0);
  ivec2 translation=ivec2(0);
  uint matrix=(control>>9u)&3u, matrix_id=(control>>11u)&3u;
  if (matrix!=0u) {
    uint base=6u+(matrix-1u)*3u;
    int scale=int(((state[base]>>22u)&3u)|(((state[base+1u]>>22u)&3u)<<2u)|
                  (((state[base+2u]>>22u)&1u)<<4u));
    if (matrix_id==0u) {
      for (uint row=0u;row<2u;++row) {
        int total=0;
        for (uint column=0u;column<3u;++column) {
          int coefficient=int(((state[base+column]>>(row*11u))&2047u)^1024u)-1024;
          total+=coefficient*values[column];
        }
        translation[row]=total>>3;
      }
    } else translation=(source*values[matrix_id-1u])/256;
    int exponent=17-scale;
    if (exponent>=0) translation>>=exponent;
    else translation=ivec2(uvec2(translation)<<uint(-exponent));
  }
  ivec2 result=ivec2(indirect_wrap(source.x,(control>>13u)&7u),
                    indirect_wrap(source.y,(control>>16u)&7u));
  // Only the low 24 bits survive. Unsigned sums also cover scaled-matrix
  // translations whose mathematical value exceeds the signed 32-bit range.
  uvec2 combined=uvec2(result)+uvec2(translation);
  if ((control&1048576u)!=0u) combined+=uvec2(previous);
  return ivec2(coordinate_bits(int(combined.x)),coordinate_bits(int(combined.y)));
}
uint pack_rgba(ivec4 value) {
  return (uint(value.r)<<24u)|(uint(value.g)<<16u)|(uint(value.b)<<8u)|uint(value.a);
}
)glsl"
R"glsl(bool shade(ivec2 pixel, inout uint depth, out ivec4 result) {
  RegisterFile registers;
  registers.r0=ivec4(state[256],state[257],state[258],state[259]);
  registers.r1=ivec4(state[260],state[261],state[262],state[263]);
  registers.r2=ivec4(state[264],state[265],state[266],state[267]);
  registers.r3=ivec4(state[268],state[269],state[270],state[271]);
  ivec4 vertex_color0=ivec4(0),vertex_color1=ivec4(0);
  for (uint i=0u;i<2u;++i) {
    ivec4 vertex_color=ivec4(0);
    if (i<((state[0]>>4u)&7u))
      for (uint c=0u;c<4u;++c) vertex_color[c]=int(clamp(plane(2u+i*4u+c,pixel),0.0,255.0));
    if (i==0u) vertex_color0=vertex_color; else vertex_color1=vertex_color;
  }
  uint last_color=0u,last_alpha=0u;
  ivec4 texture_color=ivec4(0);
  ivec4 raw_texture=ivec4(0);
  RegisterFile indirect_maps;
  indirect_maps.r0=ivec4(0); indirect_maps.r1=ivec4(0);
  indirect_maps.r2=ivec4(0); indirect_maps.r3=ivec4(0);
  uint texgens=state[0]&15u;
  uint indirect_count=(state[0]>>16u)&7u;
  for (uint stage=0u;stage<indirect_count;++stage) {
    uint order=state[39]>>(stage*6u),coordinate=(order>>3u)&7u;
    if (coordinate>=texgens) coordinate=0u;
    uint scale=state[37u+stage/2u]>>((stage&1u)*8u);
    ivec2 source=fixed_coordinates(coordinate,pixel);
    source=ivec2(source.x>>int(scale&15u),source.y>>int((scale>>4u)&15u));
    set_register(indirect_maps,stage,sample_texture(order&7u,coordinate,pixel,source));
  }
  ivec2 texture_coordinates=ivec2(0);
  uint count=((state[0]>>10u)&15u)+1u;
  for (uint stage=0u;stage<count;++stage) {
    uint order=state[40u+stage/2u]>>((stage&1u)*12u);
    uint cc=state[192u+stage*2u], ac=state[193u+stage*2u];
    uint coordinate=(order>>3u)&7u;
    if (coordinate>=texgens) coordinate=0u;
    int alpha_bump;
    uint indirect=state[16u+stage];
    texture_coordinates=indirect_coordinates(indirect,fixed_coordinates(coordinate,pixel),
                    texture_coordinates,get_register(indirect_maps,indirect&3u),alpha_bump);
    if ((order&64u)!=0u) {
      ivec4 raw=texgens!=0u ? sample_texture(order&7u,coordinate,pixel,texture_coordinates) : ivec4(0);
      raw_texture=raw;
      texture_color=swap_color(raw,(ac>>2u)&3u);
    }
    uint color_channel=(order>>7u)&7u;
    ivec4 raster=color_channel<2u ? swap_color(color_channel==0u ? vertex_color0 : vertex_color1,ac&3u) : ivec4(0);
    if (color_channel==5u || color_channel==6u)
      raster=ivec4(color_channel==5u ? alpha_bump : alpha_bump|(alpha_bump>>5));
    uint selections=state[246u+stage/2u]>>((stage&1u)*10u);
    RegisterFile args;
    for (uint operand=0u;operand<4u;++operand) {
      ivec4 argument;
      for (uint c=0u;c<3u;++c)
        argument[c]=color_operand((cc>>(12u-operand*4u))&15u,c,registers,
                                      texture_color,raster,(selections>>4u)&31u);
      argument.a=alpha_operand((ac>>(13u-operand*3u))&7u,registers,
                                   texture_color,raster,(selections>>9u)&31u);
      set_register(args,operand,operand<3u ? argument&ivec4(255) : argument);
    }
    last_color=(cc>>22u)&3u; last_alpha=(ac>>22u)&3u;
    ivec4 combined;
    for (uint c=0u;c<4u;++c) {
      uint control=c==3u ? ac : cc;
      int value;
      if (((control>>16u)&3u)==3u)
        value=args.r3[c]+(combiner_compare(control,args.r0,args.r1,c) ? args.r2[c] : 0);
      else value=combine(control,args.r0[c],args.r1[c],args.r2[c],args.r3[c],c==3u);
      combined[c]=clamp(value,
                             (control&524288u)!=0u ? 0 : -1024,
                             (control&524288u)!=0u ? 255 : 1023);
    }
    ivec4 next_register=get_register(registers,last_color);
    next_register.rgb=combined.rgb;
    set_register(registers,last_color,next_register);
    next_register=get_register(registers,last_alpha);
    next_register.a=combined.a;
    set_register(registers,last_alpha,next_register);
  }
  result=ivec4(get_register(registers,last_color).rgb,get_register(registers,last_alpha).a)&ivec4(255);
  uint offset=(uint(pixel.y)*640u+uint(pixel.x))*8u;
  uint order=uint((pixel.y/2)*320+pixel.x/2)*4u+uint((pixel.y&1)*2+(pixel.x&1));
  uint key=work[251]+order;
  if (work[252]!=0u) {
    texture_state[offset]=pack_rgba(texture_color);
    texture_state[offset+1u]=pack_rgba(raw_texture);
    atomicMax(counters[5],key);
  }
  texture_state[offset+2u]=uint(texture_coordinates.x);
  texture_state[offset+3u]=uint(texture_coordinates.y);
  atomicMax(counters[6],key);
  for (uint stage=0u;stage<indirect_count;++stage) {
    texture_state[offset+4u+stage]=pack_rgba(get_register(indirect_maps,stage));
    atomicMax(counters[7u+stage],key);
  }
  uint test=state[243];
  bool a=compare_value((test>>16u)&7u,uint(result.a),test&255u);
  bool b=compare_value((test>>19u)&7u,uint(result.a),(test>>8u)&255u);
  uint logic=test>>22u;
  bool pass=logic==0u ? a&&b : logic==1u ? a||b : logic==2u ? a!=b : a==b;
  if (!pass) return false;
  uint operation=(state[245]>>2u)&3u;
  if (operation!=0u) {
    uint value=state[244],encoding=state[245]&3u;
    if (encoding==0u) value+=uint(raw_texture.a);
    if (encoding==1u) value+=(uint(raw_texture.a)<<8u)|uint(raw_texture.r);
    if (encoding==2u) value+=(uint(raw_texture.r)<<16u)|(uint(raw_texture.g)<<8u)|uint(raw_texture.b);
    if (operation==1u) value+=depth;
    depth=value&16777215u;
  }
  uint fog_type=(state[241]>>21u)&7u;
  if (fog_type!=0u) {
    uint abits=((state[238]&524288u)<<12u)|(((state[238]>>11u)&255u)<<23u)|((state[238]&2047u)<<12u);
    uint cbits=((state[241]&524288u)<<12u)|(((state[241]>>11u)&255u)<<23u)|((state[241]&2047u)<<12u);
    precise float a=uintBitsToFloat(abits),c=uintBitsToFloat(cbits),distance;
    if (((state[238]>>11u)&255u)==255u && ((state[241]>>11u)&255u)==255u) {
      a=0.0;
      c=uintBitsToFloat((state[238]&524288u)==0u && (state[241]&524288u)==0u ? 4286578688u : 2139095040u);
    }
    if ((state[241]&1048576u)!=0u) distance=a*fog_divide(float(depth),16777215.0);
    else distance=fog_divide(a*16777215.0,float(int(state[239]-(depth>>state[240]))));
    if ((state[232]&1024u)!=0u) {
      precise float offset=fog_divide(float(pixel.x)-float(int(state[232]&1023u)-342),uintBitsToFloat(work[253]));
      precise float index=clamp(9.0-abs(offset)*9.0,0.0,9.0);
      uint lower=uint(index),upper=min(9u,lower+1u);
      precise float first=float((state[233u+lower/2u]>>((lower&1u)!=0u ? 0u : 12u))&4095u)/64.0;
      precise float second=float((state[233u+upper/2u]>>((upper&1u)!=0u ? 0u : 12u))&4095u)/64.0;
      precise float fraction=index-float(lower);
      precise float k=first*(1.0-fraction)+second*fraction;
      precise float squared=offset*offset+k*k;
      precise float adjustment=fog_divide(float(sqrt(double(squared))),k);
      distance=distance*adjustment;
    }
    precise float fog=clamp(distance-c,0.0,1.0);
    uint factor=uint(fog*256.0);
    if (fog_type>=4u) {
      uint lower=0u,upper=256u;
      while (lower<upper) {
        uint middle=lower+(upper-lower)/2u;
        if (fog>=uintBitsToFloat(fog_thresholds[(fog_type-4u)*256u+middle])) lower=middle+1u;
        else upper=middle;
      }
      factor=lower;
    }
    for (uint channel=0u;channel<3u;++channel) {
      uint fog_color=(state[242]>>(16u-channel*8u))&255u;
      result[channel]=int((uint(result[channel])*(256u-factor)+fog_color*factor)>>8u);
    }
  }
  return true;
}
ivec4 decode_efb(uint value, uint format) {
  if (format!=1u) return ivec4((value>>16u)&255u,(value>>8u)&255u,value&255u,255);
  ivec4 result=ivec4((value>>18u)&63u,(value>>12u)&63u,(value>>6u)&63u,value&63u);
  return (result<<2)|(result>>4);
}
int blend_factor(uint mode, uint channel, bool source, ivec4 value, ivec4 destination) {
  if (mode==0u) return 0;
  if (mode==1u) return 255;
  if (mode<4u) { int v=source ? destination[channel] : value[channel]; return mode==2u ? v : 255-v; }
  if (mode<6u) return mode==4u ? value.a : 255-value.a;
  return mode==6u ? destination.a : 255-destination.a;
}
uint logic_op(uint op, uint s, uint d) {
  if (op==0u) return 0u; if (op==1u) return s&d;
  if (op==2u) return s&~d; if (op==3u) return s;
  if (op==4u) return ~s&d; if (op==5u) return d;
  if (op==6u) return s^d; if (op==7u) return s|d;
  if (op==8u) return ~(s|d); if (op==9u) return ~(s^d);
  if (op==10u) return ~d; if (op==11u) return s|~d;
  if (op==12u) return ~s; if (op==13u) return ~s|d;
  if (op==14u) return ~(s&d); return 255u;
}
bool depth_test(uint offset, uint z) {
  uint control=state[64];
  if ((control&1u)==0u) return true;
  if (!compare_value((control>>1u)&7u,z,efb[337920u+offset])) return false;
  if ((control&16u)!=0u) efb[337920u+offset]=z&16777215u;
  return true;
}
void main() {
  ivec2 pixel=ivec2(gl_GlobalInvocationID.xy)+ivec2(work[0],work[1]);
  if (pixel.x>=int(work[2]) || pixel.y>=int(work[3])) return;
  ivec2 relative=pixel-ivec2(work[0],work[1]);
  for (uint i=0u;i<3u;++i) {
    int dx=int(work[4u+i*3u]),dy=int(work[5u+i*3u]),base=int(work[6u+i*3u]);
    if (base+dx*relative.y*16-dy*relative.x*16<=0) return;
  }
  uint offset=uint(pixel.y)*640u+uint(pixel.x);
  uint z=uint(clamp(plane(0u,pixel),0.0,16777215.0));
  bool early=(state[67]&64u)!=0u;
  if (early && !depth_test(offset,z)) return;
  ivec4 value;
  if (!shade(pixel,z,value)) return;
  if (!early && !depth_test(offset,z)) return;
  atomicAdd(counters[0],1u);
  uvec2 first=uvec2(pixel)&~1u,last=uvec2(pixel)|1u;
  if (counters[1]>first.x) atomicMin(counters[1],first.x);
  if (counters[2]<last.x) atomicMax(counters[2],last.x);
  if (counters[3]>first.y) atomicMin(counters[3],first.y);
  if (counters[4]<last.y) atomicMax(counters[4],last.y);
  uint format=state[67]&7u,blend=state[65];
  ivec4 destination=decode_efb(efb[offset],format);
  if ((blend&1u)!=0u) {
    if ((blend&2048u)!=0u) value=max(ivec4(0),destination-value);
    else {
      ivec4 mixed;
      for (uint c=0u;c<4u;++c) {
        int sf=blend_factor((blend>>8u)&7u,c,true,value,destination);
        int df=blend_factor((blend>>5u)&7u,c,false,value,destination);
        sf+=sf>>7; df+=df>>7;
        mixed[c]=min(255,(value[c]*sf+destination[c]*df)>>8);
      }
      value=mixed;
    }
  } else if ((blend&2u)!=0u)
    for (uint c=0u;c<4u;++c) value[c]=int(logic_op((blend>>12u)&15u,uint(value[c]),uint(destination[c]))&255u);
  if ((state[66]&256u)!=0u) value.a=int(state[66]&255u);
  if ((blend&4u)!=0u && format==1u) {
    const int dither[4]=int[4](0,2,3,1);
    for (uint c=0u;c<3u;++c) value[c]=(value[c]-(value[c]>>6)+dither[(pixel.y&1)*2+(pixel.x&1)])&252;
  }
  if ((blend&8u)==0u) value.rgb=destination.rgb;
  if ((blend&16u)==0u) value.a=destination.a;
  if (format==1u) {
    uvec4 encoded=uvec4(value)>>2u;
    efb[offset]=(encoded.r<<18u)|(encoded.g<<12u)|(encoded.b<<6u)|encoded.a;
  } else efb[offset]=(uint(value.r)<<16u)|(uint(value.g)<<8u)|uint(value.b);
}
)glsl"
