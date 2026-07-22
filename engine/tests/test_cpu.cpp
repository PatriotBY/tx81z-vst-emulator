// Sanity test for the ported HD6303X core: runs a tiny hand-assembled program
// against a flat-RAM bus and checks the resulting register/memory state.
//
// All test code/data lives at addresses >= 0x0100: addresses 0x0000-0x00FF are
// the CPU's own internal registers/RAM (real HD6303X hardware behavior - its
// internal address decoder always claims that range), so the external Bus
// never sees reads/writes below 0x0100 and test code can't be placed there.
#include "tx81z/cpu_core.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

class FlatRamBus : public tx81z::Bus
{
public:
	FlatRamBus() { mem.fill(0); }

	tx81z::u8 read(tx81z::u16 addr) override { return mem[addr]; }
	void write(tx81z::u16 addr, tx81z::u8 data) override { mem[addr] = data; }

	std::array<tx81z::u8, 0x10000> mem;
};

} // namespace

int main()
{
	FlatRamBus bus;

	// 0200: LDAA #$12 ; LDAB #$34 ; ABA ; STAA $0250 ; JMP $0207 (self-loop)
	const tx81z::u8 program[] = { 0x86, 0x12, 0xC6, 0x34, 0x1B, 0xB7, 0x02, 0x50, 0x7E, 0x02, 0x08 };
	std::memcpy(&bus.mem[0x0200], program, sizeof(program));

	bus.mem[0xFFFE] = 0x02; bus.mem[0xFFFF] = 0x00; // reset vector -> 0x0200

	m6800_cpu_device cpu(bus);
	cpu.reset();
	cpu.run(30);

	int failures = 0;

	if (bus.mem[0x0250] != 0x46)
	{
		std::printf("FAIL: mem[0x250] = 0x%02X, expected 0x46 (ABA result of 0x12+0x34)\n", bus.mem[0x0250]);
		++failures;
	}
	else
	{
		std::printf("OK: ABA computed 0x12+0x34 = 0x%02X and STAA stored it correctly\n", bus.mem[0x0250]);
	}

	if (cpu.pc() != 0x0208)
	{
		std::printf("FAIL: PC = 0x%04X, expected 0x0208 (should be looping on JMP $0208)\n", cpu.pc());
		++failures;
	}
	else
	{
		std::printf("OK: PC settled at the JMP self-loop (0x0208)\n");
	}

	// --- test 2: JSR/RTS subroutine call ---
	{
		FlatRamBus bus2;
		const tx81z::u8 prog2[] = {
			0x8E, 0x02, 0x80,       // 0200: LDS #$0280 (reset leaves S undefined - a real
			                        //       CPU/firmware always sets it up before using the stack)
			0xBD, 0x02, 0x20,       // 0203: JSR $0220
			0x7E, 0x02, 0x06,       // 0206: JMP $0206 (loop after return)
		};
		std::memcpy(&bus2.mem[0x0200], prog2, sizeof(prog2));
		bus2.mem[0x0220] = 0x86; bus2.mem[0x0221] = 0x55; // LDAA #$55
		bus2.mem[0x0222] = 0x39;                          // RTS
		bus2.mem[0xFFFE] = 0x02; bus2.mem[0xFFFF] = 0x00;

		m6800_cpu_device cpu2(bus2);
		cpu2.reset();
		cpu2.run(50);

		if (cpu2.pc() != 0x0206)
		{
			std::printf("FAIL: (JSR/RTS) PC = 0x%04X, expected 0x0206\n", cpu2.pc());
			++failures;
		}
		else
		{
			std::printf("OK: JSR $0220 / RTS returned correctly to the $0206 loop\n");
		}
	}

	// --- test 3: IRQ1 entry + RTI ---
	{
		FlatRamBus bus3;
		const tx81z::u8 prog3[] = {
			0x8E, 0x02, 0x80,       // 0200: LDS #$0280 (stack needed for the ISR's PUSHes)
			0x0E,                   // 0203: CLI
			0x7E, 0x02, 0x04,       // 0204: JMP $0204 (idle, waiting for IRQ1)
		};
		std::memcpy(&bus3.mem[0x0200], prog3, sizeof(prog3));
		bus3.mem[0x0210] = 0x86; bus3.mem[0x0211] = 0x99; // 0210: LDAA #$99
		bus3.mem[0x0212] = 0x3B;                          // 0212: RTI
		bus3.mem[0xFFFE] = 0x02; bus3.mem[0xFFFF] = 0x00; // reset vector -> 0x0200
		bus3.mem[0xFFF8] = 0x02; bus3.mem[0xFFF9] = 0x10; // IRQ1 vector -> 0x0210

		m6800_cpu_device cpu3(bus3);
		cpu3.reset();
		// Real HD6303X gates IRQ1 recognition behind RAM Control Register bit0
		// (see check_irq1_enabled()) - firmware sets this during its init
		// sequence; simulate that here since our test skips real firmware init.
		cpu3.poke(0x0014, 0x01);
		cpu3.run(24);           // LDS + CLI, settle into the idle loop
		cpu3.set_irq1(true);
		cpu3.run(14);           // enter_interrupt (12 cyc) + LDAA #$99 (2 cyc), stop right before RTI
		// Real hardware: the peripheral (e.g. the YM2414) deasserts IRQ once its
		// status is acknowledged. IRQ1 is level-sensitive, so RTI's own
		// check_irq_lines() would otherwise immediately re-enter the ISR forever
		// if we left the line asserted - simulate the firmware's ack here.
		cpu3.set_irq1(false);
		cpu3.run(20);           // RTI back to the loop, no re-trigger this time

		if (cpu3.pc() != 0x0204)
		{
			std::printf("FAIL: (IRQ1) PC = 0x%04X, expected 0x0204 (back in idle loop after RTI)\n", cpu3.pc());
			++failures;
		}
		else
		{
			std::printf("OK: IRQ1 was recognized, serviced (LDAA #$99), and RTI returned to the idle loop\n");
		}
	}

	// --- test 4: SCI transmit (MIDI Out bit-banging) ---
	{
		FlatRamBus bus4;
		bus4.mem[0xFFFE] = 0x02; bus4.mem[0xFFFF] = 0x00;

		m6800_cpu_device cpu4(bus4);
		cpu4.reset();

		cpu4.poke(0x0011, 0x02); // TRCSR: TE only (matches M6801_TRCSR_TE)
		// Real 6800-family SCI hardware only clears TDRE on a TDR write if
		// firmware first *read* TRCSR while TDRE was set (arms a one-shot
		// latch) - the standard "poll status, then write data" UART idiom.
		// Skipping this read would leave TDRE permanently set and the
		// transmitter would never actually send anything.
		cpu4.peek(0x0011);
		cpu4.poke(0x0013, 0xA5); // TDR: byte to transmit

		// Sample the tx line once per 16 clock_serial() ticks (one UART bit
		// period, per hd6301x's m_sclk_divider - the same 500kHz/16 = 31250
		// baud division the real TX81Z hardware uses for MIDI).
		std::vector<bool> bits;
		for (int i = 0; i < 40; ++i)
		{
			for (int t = 0; t < 16; ++t)
				cpu4.clock_serial();
			bits.push_back(cpu4.tx_line());
		}

		// Skip the leading run of '1's (idle / TX_STATE_INIT warm-up), then
		// expect: start(0), 8 data bits LSB-first, stop(1).
		size_t i = 0;
		while (i < bits.size() && bits[i]) ++i;

		bool ok = (i + 10) <= bits.size() && !bits[i];
		u8 decoded = 0;
		if (ok)
		{
			for (int b = 0; b < 8; ++b)
				decoded |= (bits[i + 1 + b] ? 1 : 0) << b;
			ok = ok && bits[i + 9]; // stop bit
		}

		if (!ok || decoded != 0xA5)
		{
			std::printf("FAIL: (SCI TX) decoded byte = 0x%02X (ok=%d), expected 0xA5\n", decoded, ok);
			++failures;
		}
		else
		{
			std::printf("OK: SCI transmitted TDR=0xA5 correctly as start+8 data(LSB-first)+stop bits\n");
		}
	}

	// --- test 5: SCI receive (MIDI In bit-banging) ---
	{
		FlatRamBus bus5;
		bus5.mem[0xFFFE] = 0x02; bus5.mem[0xFFFF] = 0x00;

		m6800_cpu_device cpu5(bus5);
		cpu5.reset();

		bool rx_bit = true; // idle = mark = 1
		cpu5.port2_input = [&rx_bit]() -> tx81z::u8 { return rx_bit ? 0xff : 0xf7; }; // bit3 = MIDI In

		cpu5.poke(0x0011, 0x08); // TRCSR: RE only (M6801_TRCSR_RE)

		// 0xA5 = 1010_0101 -> LSB-first data bits: 1,0,1,0,0,1,0,1
		const bool sequence[] = { false, true, false, true, false, false, true, false, true, true }; // start,d0..d7,stop
		for (bool bit : sequence)
		{
			rx_bit = bit;
			for (int t = 0; t < 16; ++t)
				cpu5.clock_serial();
		}

		u8 rdr = cpu5.peek(0x0012);
		u8 trcsr = cpu5.peek(0x0011);

		if (rdr != 0xA5 || !(trcsr & 0x80))
		{
			std::printf("FAIL: (SCI RX) RDR = 0x%02X, TRCSR = 0x%02X, expected RDR=0xA5 with RDRF set\n", rdr, trcsr);
			++failures;
		}
		else
		{
			std::printf("OK: SCI received a bit-banged 0xA5 byte correctly (RDR=0xA5, RDRF set)\n");
		}
	}

	if (failures == 0)
		std::printf("\nAll CPU core sanity checks passed.\n");
	else
		std::printf("\n%d check(s) FAILED.\n", failures);

	return failures == 0 ? 0 : 1;
}
