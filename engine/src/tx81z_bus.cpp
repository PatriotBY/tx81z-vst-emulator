#include "tx81z/tx81z_bus.h"

#include <algorithm>
#include <stdexcept>

namespace tx81z {

Tx81zBus::Tx81zBus()
	: m_opz(*this)
{
	m_nvram.fill(0); // matches nvram_device::DEFAULT_ALL_0 in ymtx81z.cpp
}

void Tx81zBus::load_rom(std::vector<u8> rom)
{
	if (rom.size() != 0x10000)
		throw std::runtime_error("TX81Z ROM image must be exactly 0x10000 (64KB) bytes");
	m_rom = std::move(rom);
}

void Tx81zBus::load_nvram(const std::vector<u8> &nvram)
{
	if (nvram.size() != m_nvram.size())
		throw std::runtime_error("TX81Z NVRAM image must be exactly 0x2000 (8KB) bytes");
	std::copy(nvram.begin(), nvram.end(), m_nvram.begin());
}

u8 Tx81zBus::read(u16 addr)
{
	if (addr >= 0x2000 && addr < 0x4000)
		return m_opz.read(addr & 0x0001);
	if (addr >= 0x4000 && addr < 0x6000)
		return m_lcd.read(static_cast<u8>(addr & 0x0001));
	if (addr >= 0x6000 && addr < 0x8000)
		return m_nvram[addr - 0x6000];
	if (addr >= 0x8000)
	{
		size_t off = size_t(m_rom_bank) * 0x8000 + (addr - 0x8000);
		return (off < m_rom.size()) ? m_rom[off] : 0xff;
	}
	return 0xff; // 0x0100-0x1fff: unmapped/open bus
}

void Tx81zBus::write(u16 addr, u8 data)
{
	if (addr >= 0x2000 && addr < 0x4000)
	{
		if (opz_write_debug)
			opz_write_debug(addr & 0x0001, data);
		m_opz.write(addr & 0x0001, data);
		return;
	}
	if (addr >= 0x4000 && addr < 0x6000)
	{
		m_lcd.write(static_cast<u8>(addr & 0x0001), data);
		return;
	}
	if (addr >= 0x6000 && addr < 0x8000)
	{
		m_nvram[addr - 0x6000] = data;
		m_nvram_dirty = true;
		return;
	}
	// 0x8000-0xffff is ROM (read-only) and 0x0100-0x1fff is unmapped: writes ignored
}

} // namespace tx81z
