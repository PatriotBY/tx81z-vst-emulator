// The TX81Z's actual external memory map (everything the CPU core's Bus
// interface sees at addresses 0x0100-0xFFFF), ported from
// src/mame/yamaha/ymtx81z.cpp's mem_map():
//
//   0x2000-0x3fff (mirror 0x1ffe, i.e. only bit0 of the low 13 bits matters):
//       YM2414 (OPZ) - address port / data+status port
//   0x4000-0x5fff (mirror 0x1ffe): HD44780 LCD controller
//   0x6000-0x7fff: 8KB battery-backed NVRAM (patch storage)
//   0x8000-0xffff: 32KB banked window into the 64KB ROM image (bank
//       selected by the CPU's port6 bit3 - see Tx81zBus::set_rom_bank)
#pragma once

#include "tx81z/bus.h"
#include "tx81z/hd44780.h"
#include "ymfm_opz.h"

#include <array>
#include <atomic>
#include <functional>
#include <vector>

namespace tx81z {

class Tx81zBus : public Bus, public ymfm::ymfm_interface
{
public:
	Tx81zBus();

	// rom must be exactly 0x10000 (64KB) bytes - one of the tx81z-v1.x.icXX
	// firmware dumps
	void load_rom(std::vector<u8> rom);

	// driven by the CPU's port6_output callback (bit3 = bank select)
	void set_rom_bank(int bank) { m_rom_bank = bank & 1; }

	// nvram must be exactly 0x2000 (8KB) bytes - a real dump (e.g. MAME's
	// nvram_device plain-RAM save file) of the battery-backed patch memory.
	// Loading a real, factory-reset dump sidesteps the blank/dead-battery
	// cold-start state entirely (no NVRAM header/checksum to speak of - it's
	// just the raw RAM contents, see src/devices/machine/nvram.cpp).
	void load_nvram(const std::vector<u8> &nvram);

	u8 read(u16 addr) override;
	void write(u16 addr, u8 data) override;

	// called once per audio sample at the OPZ's native rate (see
	// opz_sample_rate()) to advance FM synthesis and produce one sample
	void generate_audio(ymfm::ym2414::output_data *out) { m_opz.generate(out, 1); }
	uint32_t opz_sample_rate(uint32_t input_clock) const { return m_opz.sample_rate(input_clock); }

	// wired to the CPU's set_irq1() by the harness that owns both objects
	std::function<void(bool asserted)> irq1_callback;

	// Debug hook: fires on every write the CPU makes to the OPZ's
	// address/data ports (offset 0/1) - lets a diagnostic harness see
	// whether firmware is actually touching the chip at all.
	std::function<void(u8 offset, u8 data)> opz_write_debug;

	Hd44780 &lcd() { return m_lcd; }

	// Real hardware's NVRAM is just battery-backed SRAM - it retains state
	// automatically, there's no explicit "save" step. To get the same effect
	// across plugin instances/DAW restarts, the host polls nvram_dirty() and
	// persists nvram_snapshot() to disk periodically (see
	// PluginProcessor::flushNvramIfDirty). Snapshot/dirty-check are safe to
	// call from a different thread than the one calling write() - at worst a
	// snapshot catches a write "mid-flight" and is one field behind, which
	// only matters for a periodic background save, not for correctness.
	bool nvram_dirty() const { return m_nvram_dirty; }
	void clear_nvram_dirty() { m_nvram_dirty = false; }
	std::vector<u8> nvram_snapshot() const { return std::vector<u8>(m_nvram.begin(), m_nvram.end()); }

protected:
	// ymfm::ymfm_interface override
	void ymfm_update_irq(bool asserted) override
	{
		if (irq1_callback)
			irq1_callback(asserted);
	}

private:
	ymfm::ym2414 m_opz;
	Hd44780 m_lcd;
	std::array<u8, 0x2000> m_nvram{}; // 0x6000-0x7fff, default all-zero (matches nvram_device::DEFAULT_ALL_0)
	std::vector<u8> m_rom;            // 0x10000 bytes when loaded
	int m_rom_bank = 0;
	std::atomic<bool> m_nvram_dirty{ false };
};

} // namespace tx81z
