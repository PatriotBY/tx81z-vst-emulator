// Abstract external memory bus for addresses 0x0100-0xFFFF (everything outside
// the HD6303X's own internal register/RAM window at 0x0000-0x00FF, which the
// CPU core handles itself). A concrete implementation wires up the TX81Z's
// actual memory map: YM2414/OPZ chip, HD44780 LCD, 8KB NVRAM, banked ROM.
#pragma once

#include "tx81z/types.h"

namespace tx81z {

class Bus
{
public:
	virtual ~Bus() = default;

	virtual u8 read(u16 addr) = 0;
	virtual void write(u16 addr, u8 data) = 0;
};

} // namespace tx81z
