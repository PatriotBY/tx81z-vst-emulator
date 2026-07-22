// Portable register-pair type matching MAME's PAIR union (emu/emucore.h) for a
// little-endian host. Field names (w.l/w.h/b.l/b.h) are kept identical to MAME's
// so that 6800ops.hxx (copied verbatim from MAME, license:BSD-3-Clause,
// copyright-holders:Aaron Giles) compiles unmodified.
#pragma once

#include <cstdint>

namespace tx81z {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;

union PAIR
{
	struct { u8 l, h, h2, h3; } b;
	struct { u16 l, h; } w;
	u32 d;
};

} // namespace tx81z
