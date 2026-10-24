// SPDX-License-Identifier: GPL-2.0-or-later
R"glsl(#version 430 core
layout(local_size_x=1) in;
layout(std430,binding=2) readonly buffer Counters { uint counters[]; };
layout(std430,binding=5) readonly buffer Retained { uint retained[]; };
layout(std430,binding=7) writeonly buffer Packet { uint packet[]; };

uint retained_offset(uint key) {
  uint order=key&1048575u;
  uint block=order/4u;
  uint x=(block%320u)*2u+(order&1u);
  uint y=(block/320u)*2u+((order>>1u)&1u);
  return (y*640u+x)*8u;
}

void main() {
  packet[0]=counters[0];
  packet[1]=counters[1];
  packet[2]=counters[2];
  packet[3]=counters[3];
  packet[4]=counters[4];
  packet[5]=counters[5];
  packet[6]=counters[6];
  packet[7]=counters[7];
  packet[8]=counters[8];
  packet[9]=counters[9];
  packet[10]=counters[10];
  packet[11]=0u;
  packet[12]=0u;
  packet[13]=0u;
  packet[14]=0u;
  packet[15]=0u;
  packet[16]=0u;
  packet[17]=0u;
  packet[18]=0u;
  uint texture_key=counters[5];
  uint coordinate_key=counters[6];
  uint map0_key=counters[7];
  uint map1_key=counters[8];
  uint map2_key=counters[9];
  uint map3_key=counters[10];
  if (texture_key!=0u) {
    uint texture_base=retained_offset(texture_key);
    packet[11]=retained[texture_base+0u];
    packet[12]=retained[texture_base+1u];
  }
  if (coordinate_key!=0u) {
    uint coordinate_base=retained_offset(coordinate_key);
    packet[13]=retained[coordinate_base+2u];
    packet[14]=retained[coordinate_base+3u];
  }
  if (map0_key!=0u) {
    uint map0_base=retained_offset(map0_key);
    packet[15]=retained[map0_base+4u];
  }
  if (map1_key!=0u) {
    uint map1_base=retained_offset(map1_key);
    packet[16]=retained[map1_base+5u];
  }
  if (map2_key!=0u) {
    uint map2_base=retained_offset(map2_key);
    packet[17]=retained[map2_base+6u];
  }
  if (map3_key!=0u) {
    uint map3_base=retained_offset(map3_key);
    packet[18]=retained[map3_base+7u];
  }
}
)glsl"
