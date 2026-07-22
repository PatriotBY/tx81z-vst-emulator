// Diagnostic tool (not a pass/fail test): loads a real TX81Z firmware dump and
// runs it against the ported CPU + system bus, printing the LCD contents
// periodically so we can see whether firmware actually boots.
#include "tx81z/cpu_core.h"
#include "tx81z/tx81z_bus.h"

#include <cstdio>
#include <fstream>
#include <vector>

namespace {

constexpr double CPU_CLOCK_HZ = 7159090.0;   // 7.15909 MHz XTAL
constexpr double MIDI_CLOCK_HZ = 500000.0;   // separate 500kHz XTAL for the SCI

std::vector<tx81z::u8> load_file(const char *path)
{
	std::ifstream f(path, std::ios::binary);
	if (!f)
		throw std::runtime_error(std::string("cannot open ") + path);
	return std::vector<tx81z::u8>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

} // namespace

int main(int argc, char **argv)
{
	const char *rom_path = (argc > 1) ? argv[1] : "roms/tx81z-v1.6.ic15";
	double seconds_to_run = (argc > 2) ? std::atof(argv[2]) : 2.0;

	std::vector<tx81z::u8> rom;
	try
	{
		rom = load_file(rom_path);
	}
	catch (const std::exception &e)
	{
		std::fprintf(stderr, "Failed to load ROM: %s\n", e.what());
		return 1;
	}

	if (rom.size() != 0x10000)
	{
		std::fprintf(stderr, "ROM file %s is %zu bytes, expected exactly 65536\n", rom_path, rom.size());
		return 1;
	}

	tx81z::Tx81zBus bus;
	bus.load_rom(rom);

	m6800_cpu_device cpu(bus);
	bus.irq1_callback = [&cpu](bool asserted) { cpu.set_irq1(asserted); };

	int leds = 0;
	cpu.port6_output = [&](tx81z::u8 data, tx81z::u8 /*ddr*/)
	{
		bus.set_rom_bank((data >> 3) & 1);
		leds = (data >> 4) & 0x0f;
	};

	// TX81Z has no front-panel buttons/cassette/clock pins driven for this
	// diagnostic - present them as "not pressed" (idle-high, active-low buttons).
	cpu.port2_input = [] { return tx81z::u8(0xff); };
	cpu.port5_input = [] { return tx81z::u8(0xff); };
	cpu.port6_input = [] { return tx81z::u8(0xff); };

	cpu.reset();

	const long long total_cycles = (long long)(CPU_CLOCK_HZ * seconds_to_run);
	const int chunk_cycles = 200; // roughly one audio sample's worth - keeps IRQ latency low
	long long cycles_run = 0;
	double midi_accum = 0.0;

	std::string last_lcd;
	long long next_report_at = 0;
	const long long report_every = (long long)(CPU_CLOCK_HZ * 0.1); // every ~100ms simulated

	while (cycles_run < total_cycles)
	{
		int actual = cpu.run(chunk_cycles);
		cycles_run += actual;

		midi_accum += actual * (MIDI_CLOCK_HZ / CPU_CLOCK_HZ);
		while (midi_accum >= 1.0)
		{
			cpu.clock_serial();
			midi_accum -= 1.0;
		}

		if (cycles_run >= next_report_at)
		{
			next_report_at += report_every;
			std::string lcd = bus.lcd().display_text(2, 16);
			if (lcd != last_lcd)
			{
				double t = cycles_run / CPU_CLOCK_HZ;
				std::printf("--- t=%.3fs  PC=%04X  LEDs=%X ---\n%s\n", t, cpu.pc(), leds, lcd.c_str());
				last_lcd = lcd;
			}
		}
	}

	std::printf("\nFinished %lld cycles (%.2fs simulated). Final PC=%04X\n", cycles_run, cycles_run / CPU_CLOCK_HZ, cpu.pc());
	std::printf("Final LCD:\n%s\n", bus.lcd().display_text(2, 16).c_str());

	return 0;
}
