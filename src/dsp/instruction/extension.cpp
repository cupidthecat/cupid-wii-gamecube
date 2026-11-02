// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/dsp/instruction.hpp"
#include "operations.hpp"
#include <stdexcept>

namespace cupid::dsp {
void InstructionCore::execute_extension(detail::Operation operation, u16 word) {
  using O = detail::Operation;
  const auto &r = state_.registers;
  const unsigned address = word & 3;
  const auto next_address = [&](unsigned reg, bool indexed) {
    return indexed ? address_update(reg, s16(r[4 + reg])) : address_increment(reg);
  };
  switch (operation) {
  case O::nop_ext:
    return;
  case O::dr:
    pending(address, address_decrement(address));
    return;
  case O::ir:
    pending(address, address_increment(address));
    return;
  case O::nr:
    pending(address, address_update(address, s16(r[4 + address])));
    return;
  case O::mv:
    pending(24 + ((word >> 2) & 3), read_register(28 + (word & 3)));
    return;
  case O::s:
  case O::sn:
    write_data(r[address], read_register(28 + ((word >> 3) & 3)));
    pending(address, next_address(address, operation == O::sn));
    return;
  case O::l:
  case O::ln:
    extend_pending_middle(24 + ((word >> 3) & 7), read_data(r[address]));
    pending(address, next_address(address, operation == O::ln));
    return;
  case O::ls:
  case O::lsn:
  case O::lsm:
  case O::lsnm:
  case O::sl:
  case O::sln:
  case O::slm:
  case O::slnm: {
    const bool reversed = (word & 2) != 0;
    // Store precedes load even when both addresses refer to the same word.
    write_data(r[reversed ? 0 : 3], read_register(30 + (word & 1)));
    pending(24 + ((word >> 4) & 3), read_data(r[reversed ? 3 : 0]));
    pending(3, next_address(3, (word & 8) != 0));
    pending(0, next_address(0, (word & 4) != 0));
    return;
  }
  case O::ld:
  case O::ldn:
  case O::ldm:
  case O::ldnm:
  case O::ldax:
  case O::ldaxn:
  case O::ldaxm:
  case O::ldaxnm: {
    const bool pair = operation == O::ldax || operation == O::ldaxn || operation == O::ldaxm ||
                      operation == O::ldaxnm;
    const unsigned first_address = pair ? ((word >> 5) & 1) : address;
    const unsigned first_destination = pair ? 26 + ((word >> 4) & 1) : 24 + 2 * ((word >> 5) & 1);
    const unsigned second_destination = pair ? 24 + ((word >> 4) & 1) : 25 + 2 * ((word >> 4) & 1);
    const u16 second_address = (r[first_address] >> 10) == (r[3] >> 10) ? r[first_address] : r[3];
    pending(first_destination, read_data(r[first_address]));
    // Same-area reads remain two transactions, including hardware side effects.
    pending(second_destination, read_data(second_address));
    pending(first_address, next_address(first_address, (word & 4) != 0));
    pending(3, next_address(3, (word & 8) != 0));
    return;
  }
  default:
    throw std::logic_error("DSP extension opcode has no implementation");
  }
}
} // namespace cupid::dsp
