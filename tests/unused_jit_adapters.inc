// SPDX-License-Identifier: GPL-2.0-or-later

// These fixtures never construct a compiled CPU core. Complete its virtual
// interface so checked unique_ptr destruction has real RTTI under UBSan.
// An unexpected core lifetime or mapping query remains a hard failure.
void (*unused_jit_destructor)() = [] { std::terminate(); };

JitBase::~JitBase() {
  unused_jit_destructor();
}

bool JitBase::WantsPageTableMappings() const {
  unused();
}
