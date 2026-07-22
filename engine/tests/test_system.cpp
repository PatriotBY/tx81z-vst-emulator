// Integration test for the CPU core wired to the real TX81Z external memory
// map (Tx81zBus: OPZ chip via ymfm, HD44780 LCD, NVRAM, banked ROM) - checks
// the wiring itself, not real firmware (no ROM dump available yet).
#include "tx81z/cpu_core.h"
#include "tx81z/tx81z_bus.h"

#include <cstdio>
#include <cstring>
#include <vector>

int main()
{
	int failures = 0;

	tx81z::Tx81zBus bus;

	// Build a synthetic 64KB "ROM" image: bank0 has one test program, bank1
	// has a different marker byte at the same offset, to prove bank switching
	// (port6 bit3, the real TX81Z's ROM bank select) actually reaches the CPU.
	std::vector<tx81z::u8> rom(0x10000, 0xff);
	// bank0 (rom[0x0000-0x7fff], mapped at CPU 0x8000-0xffff):
	// reset vector -> 0x8000; program writes to LCD + OPZ then loops.
	rom[0x7ffe] = 0x80; rom[0x7fff] = 0x00; // reset vector -> 0x8000
	{
		const tx81z::u8 program[] = {
			0x8E, 0x02, 0x80,       // LDS #$0280 (external RAM used as stack scratch space)
			0x86, 0x01,             // LDAA #$01      (OPZ register address 0x01 = noise freq/enable)
			0xB7, 0x20, 0x00,       // STAA $2000     (OPZ address port)
			0x86, 0x00,             // LDAA #$00
			0xB7, 0x20, 0x01,       // STAA $2001     (OPZ data port)
			0x86, 0x38,             // LDAA #$38      (HD44780 function set: 8-bit, 2-line, 5x8)
			0xB7, 0x40, 0x00,       // STAA $4000     (LCD control port)
			0x86, 'H',              // LDAA #'H'
			0xB7, 0x40, 0x01,       // STAA $4001     (LCD data port)
			0x86, 'I',              // LDAA #'I'
			0xB7, 0x40, 0x01,       // STAA $4001
			0x86, 0x42,             // LDAA #$42      (arbitrary NVRAM test byte)
			0xB7, 0x60, 0x00,       // STAA $6000     (NVRAM)
			0x7E, 0x80, 0x21,       // JMP $8021 (self-loop - this instruction's own address)
		};
		std::memcpy(&rom[0x0000], program, sizeof(program));
	}
	// bank1 (rom[0x8000-0xffff], also mapped at CPU 0x8000-0xffff when bank
	// selected): distinct marker byte so the test can tell banks apart.
	// Uses offset 0x0100/0x8100 - well clear of the bank0 program at 0x0000+.
	rom[0x8100] = 0xAA;
	rom[0x0100] = 0x55; // bank0's marker at the same relative offset, for contrast

	bus.load_rom(rom);

	m6800_cpu_device cpu(bus);
	bus.irq1_callback = [&cpu](bool asserted) { cpu.set_irq1(asserted); };
	bus.set_rom_bank(0);

	cpu.reset();
	cpu.run(60); // enough for the whole program above, then looping on the JMP

	// --- ROM bank 0 program executed correctly ---
	if (cpu.pc() != 0x8021)
	{
		std::printf("FAIL: PC = 0x%04X, expected 0x8021 (looping after running the bank0 program)\n", cpu.pc());
		++failures;
	}
	else
	{
		std::printf("OK: CPU fetched and ran a program straight out of banked ROM (0x8000+)\n");
	}

	// --- LCD received the writes through the bus ---
	std::string lcd_text = bus.lcd().display_text(2, 16);
	if (lcd_text.substr(0, 2) != "HI")
	{
		std::printf("FAIL: LCD display_text() = \"%s\", expected it to start with \"HI\"\n", lcd_text.c_str());
		++failures;
	}
	else
	{
		std::printf("OK: LCD controller received CPU writes through the bus (shows \"HI...\")\n");
	}

	// --- NVRAM received the write ---
	if (cpu.peek(0x6000) != 0x42)
	{
		std::printf("FAIL: NVRAM[0] = 0x%02X, expected 0x42\n", cpu.peek(0x6000));
		++failures;
	}
	else
	{
		std::printf("OK: NVRAM (0x6000-0x7fff) received the CPU's write\n");
	}

	// --- ROM bank switching (port6 bit3) actually changes what the CPU reads ---
	{
		tx81z::Tx81zBus bus2;
		bus2.load_rom(rom);
		m6800_cpu_device cpu2(bus2);

		bus2.set_rom_bank(0);
		tx81z::u8 bank0_byte = cpu2.peek(0x8100);
		bus2.set_rom_bank(1);
		tx81z::u8 bank1_byte = cpu2.peek(0x8100);

		if (bank0_byte != 0x55 || bank1_byte != 0xAA)
		{
			std::printf("FAIL: bank0 byte = 0x%02X (expected 0x55), bank1 byte = 0x%02X (expected 0xAA)\n", bank0_byte, bank1_byte);
			++failures;
		}
		else
		{
			std::printf("OK: ROM bank switching (0x8000-0xffff window) selects the correct 32KB half\n");
		}
	}

	// --- port6 output callback fires with the CPU's actual pin state ---
	{
		tx81z::Tx81zBus bus3;
		bus3.load_rom(rom);
		m6800_cpu_device cpu3(bus3);

		int last_bank = -1;
		int leds = -1;
		cpu3.port6_output = [&](tx81z::u8 data, tx81z::u8 /*ddr*/)
		{
			last_bank = (data >> 3) & 1;
			leds = (data >> 4) & 0x0f;
		};

		cpu3.reset();
		cpu3.poke(0x0016, 0xff); // port6 DDR: all output
		cpu3.poke(0x0017, 0xA8); // 1010_1000: bit3 set (bank=1), bits5+7 set (2 of the 4 LEDs on)

		if (last_bank != 1 || leds != 0b1010)
		{
			std::printf("FAIL: port6_output saw bank=%d leds=%d, expected bank=1 leds=0b1010\n", last_bank, leds);
			++failures;
		}
		else
		{
			std::printf("OK: CPU port6 output callback correctly reports ROM bank select + LED bits\n");
		}
	}

	if (failures == 0)
		std::printf("\nAll system-wiring integration checks passed.\n");
	else
		std::printf("\n%d check(s) FAILED.\n", failures);

	return failures == 0 ? 0 : 1;
}
