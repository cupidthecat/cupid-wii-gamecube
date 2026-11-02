; SPDX-License-Identifier: GPL-2.0-or-later
PUBLIC cupid_host_float_enter, cupid_host_float_restore

.const
ALIGN 4
default_mxcsr DWORD 01f80h
; Legacy 32-bit-operand x87 environment: CW, SW, tags, then zero pointers/opcode.
; Reserved halfwords are zero; FLDENV ignores them. TOP and condition codes are
; explicitly zero instead of depending on the undefined flags of FLDCW.
default_environment DWORD 0000027fh, 0, 0000ffffh, 0, 0, 0, 0

.code
; RCX points to the 28-byte x87 environment followed by the raw MXCSR.
; These leaf routines do not change RSP, nonvolatile GPRs or XMM payloads.
cupid_host_float_enter PROC
  stmxcsr dword ptr [rcx+28]
  fnstenv [rcx]
  fnclex
  fldenv [default_environment]
  ldmxcsr dword ptr [default_mxcsr]
  ret
cupid_host_float_enter ENDP

cupid_host_float_restore PROC
  ; Discard only pending exceptions from the guarded work before loading the
  ; caller's environment. Do not execute a waiting x87 instruction afterward.
  fnclex
  fldenv [rcx]
  ldmxcsr dword ptr [rcx+28]
  ret
cupid_host_float_restore ENDP
END
