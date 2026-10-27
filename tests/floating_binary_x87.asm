; SPDX-License-Identifier: GPL-2.0-or-later
PUBLIC fp_x87_control, fp_x87_status, fp_x87_set_control, fp_x87_set_flags
.code
fp_x87_control PROC
  sub rsp,8
  fnstcw word ptr [rsp]
  movzx eax,word ptr [rsp]
  add rsp,8
  ret
fp_x87_control ENDP
fp_x87_status PROC
  fnstsw ax
  movzx eax,ax
  ret
fp_x87_status ENDP
fp_x87_set_control PROC
  sub rsp,8
  mov word ptr [rsp],cx
  fldcw word ptr [rsp]
  add rsp,8
  ret
fp_x87_set_control ENDP
fp_x87_set_flags PROC
  sub rsp,40
  fnstenv [rsp]
  and byte ptr [rsp+4],0c0h
  and cl,03fh
  or byte ptr [rsp+4],cl
  fldenv [rsp]
  add rsp,40
  ret
fp_x87_set_flags ENDP
END
