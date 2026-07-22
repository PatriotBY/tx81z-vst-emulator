// Standalone port of MAME's HD6303X CPU core (src/devices/cpu/m6800/{m6800,m6801}.cpp,
// both license:BSD-3-Clause copyright-holders:Aaron Giles), adapted to run outside MAME's
// device/address_map framework for the TX81Z-VST project.
//
// The class is deliberately still named `m6800_cpu_device` (not namespaced) so that
// 6800ops.hxx - copied byte-for-byte from MAME - compiles without any modification: its
// OP_HANDLER macro expands each opcode body to `void m6800_cpu_device::<name>()`.
//
// Only the opcode table actually used by the real hardware chain
// (m6800 -> m6801 -> hd6301 -> hd6301x -> hd6303x) is ported: hd63701_insn/cycles_63701,
// since that's what hd6301x_cpu_device's constructor selects in MAME's m6801.cpp.
#pragma once

#include "tx81z/types.h"
#include "tx81z/bus.h"

#include <functional>

using tx81z::PAIR;
using tx81z::u8;
using tx81z::u16;
using tx81z::u32;
using tx81z::s8;
using tx81z::s16;
using tx81z::s32;

class m6800_cpu_device
{
public:
	typedef void (m6800_cpu_device::*op_func)();

	explicit m6800_cpu_device(tx81z::Bus &bus);
	virtual ~m6800_cpu_device() = default;

	// full chip reset (RES pin) - loads PC from the reset vector at 0xfffe
	void reset();

	// runs until at least `cycles` worth of clock cycles have elapsed;
	// returns the actual number of cycles consumed (execute_run equivalent)
	int run(int cycles);

	// IRQ1 line (external interrupt, e.g. YM2414 -> CPU IRQ1 in the real TX81Z)
	void set_irq1(bool asserted);
	void set_nmi(bool asserted);

	u16 pc() const { return m_pc.w.l; }

	// Debug/test helper: pokes an address exactly like the CPU itself would
	// (0x0000-0x00FF hits internal registers/RAM, everything else the Bus).
	void poke(u16 addr, u8 data) { write(addr, data); }
	u8 peek(u16 addr) { return read(addr); }

	// Debug/test helper: current SCI transmit line level (see serial_transmit()).
	bool tx_line() const { return m_tx != 0; }

	// Debug hook: fires whenever the SCI finishes receiving a byte into RDR
	// (independent of whether firmware ever reads it) - lets a diagnostic
	// harness confirm the UART itself worked even if higher-level firmware
	// behavior is in question.
	std::function<void(u8 byte)> on_sci_byte_received;
	// Debug hook: fires on overrun (RDRF was still set when the next byte's
	// stop bit arrived - firmware hadn't read the previous byte in time) or
	// framing errors (stop bit wasn't 1). code: 1=overrun, 2=framing.
	std::function<void(int code)> on_sci_error;

	// --- external wiring for the internal peripherals actually used by the TX81Z
	// (only what hd6303x_io/hd6303x_mem map in MAME's m6801.cpp: port2, port5,
	// port6, the timer, and the SCI/UART - ports 1/3/4/7 are unmapped for this
	// exact chip usage since those pins are repurposed as the external bus) ---

	// port2 (0x0001 ddr, 0x0003 data): bit3 = MIDI In raw bit, bit4 = SCI TXD
	// (internally overlaid), bit5 = Cursor button, bit6/7 = Master Volume +/-.
	// Called whenever the CPU reads the port; return the live input-pin state.
	std::function<u8()> port2_input;

	// port5 (0x0015, read-only data): Store/Eq Copy button, MR signal, Data
	// Entry Inc/Dec, Voice Parameter +/-.
	std::function<u8()> port5_input;

	// port6 (0x0016 ddr, 0x0017 data): bits 0-2 = Play/Perform, Edit/Compare,
	// Utility buttons (input); bit3 = ROM bank select, bits4-7 = LEDs (output).
	std::function<u8()> port6_input;
	std::function<void(u8 data, u8 ddr)> port6_output;

	// SCI transmit pin (TXD): called on every bit transition with the new line
	// state. Feeds MIDI Out - the byte-level UART assembly happens outside the
	// CPU core, same separation MAME itself uses.
	std::function<void(bool state)> serial_tx_output;

	// Advances the SCI's external bit clock by one tick (real hardware: a
	// dedicated 500kHz crystal feeding this line, divided internally by 16 to
	// give the standard 31250 baud MIDI rate - see hd6301x's m_sclk_divider).
	void clock_serial();

protected:
	enum
	{
		M6800_WAI = 8,   // set when WAI is waiting for an interrupt
		M6800_SLP = 0x10 // HD63701/HD6303 SLP
	};

	tx81z::Bus &m_bus;

	// 0x0000-0x00FF is the CPU's own internal registers/RAM (see internal_read/
	// internal_write below); everything else goes to the external Bus.
	u8 read(u16 addr) { return addr < 0x0100 ? internal_read(addr) : m_bus.read(addr); }
	void write(u16 addr, u8 data) { if (addr < 0x0100) internal_write(addr, data); else m_bus.write(addr, data); }

	PAIR m_ppc;   // previous program counter
	PAIR m_pc;    // program counter
	PAIR m_s;     // stack pointer
	PAIR m_x;     // index register
	PAIR m_d;     // accumulators (A:B)
	PAIR m_ea;    // effective address (scratch)
	u8 m_cc = 0;  // condition codes
	u8 m_wai_state = 0;
	u8 m_nmi_state = 0;
	u8 m_nmi_pending = 0;
	u8 m_irq_state[5] = {}; // [IRQ1, TIN, IS3, IRQ2, ...]

	const op_func *m_insn = nullptr;
	const u8 *m_cycles = nullptr;

	int m_icount = 0;

	static const u8 flags8i[256]; // increment
	static const u8 flags8d[256]; // decrement

	static const op_func hd63701_insn[0x100];
	static const u8 cycles_63701[256];

	u32 RM16(u32 addr);
	void WM16(u32 addr, PAIR *p);
	void enter_interrupt(const char *message, u16 irq_vector);
	virtual bool check_irq1_enabled();
	virtual void check_irq2();
	void check_irq_lines();

	virtual void increment_counter(int amount);
	virtual void eat_cycles();
	virtual void cleanup_counters();
	virtual void take_trap();

	virtual void execute_one();

	static void logerror(const char *fmt, ...);

	// --- internal address space (0x0000-0x00FF): registers + internal RAM,
	// ported from MAME's m6801.cpp (m6801_cpu_device / hd6301x_cpu_device),
	// scoped to only the registers hd6303x_io/hd6303x_mem actually map ---

	u8 m_internal_ram[192] = {}; // mapped at 0x0040-0x00FF

	// port2
	u8 m_port2_ddr = 0, m_port2_data = 0;
	bool m_port2_written = false;
	u8 p2_data_r();
	void p2_data_w(u8 data);
	void p2_ddr_w(u8 data);
	void write_port2();

	// port5 (hd6301x extension, read-only)
	u8 p5_data_r();

	// port6 (hd6301x extension)
	u8 m_port6_ddr = 0, m_port6_data = 0;
	u8 p6_data_r();
	void p6_data_w(u8 data);
	void p6_ddr_w(u8 data);

	// free-running counter / output compare / timer control (0x08-0x0e, 0x0f, 0x19-0x1a)
	PAIR m_counter{};              // CT
	PAIR m_output_compare[2]{};    // OC (index 0), OC2 (index 1, hd6301x only)
	PAIR m_timer_over{};           // TOH/TOD bookkeeping for cleanup_counters
	u16 m_input_capture = 0;
	u8 m_tcsr = 0, m_pending_tcsr = 0;
	u8 m_tcsr2 = 0, m_pending_tcsr2 = 0;
	u8 m_latch09 = 0;
	u32 m_timer_next = 0xffff;

	u8 tcsr_r(); void tcsr_w(u8 data);
	u8 ch_r(); u8 cl_r(); void ch_w(u8 data); void cl_w(u8 data);
	u8 ocrh_r(); u8 ocrl_r(); void ocrh_w(u8 data); void ocrl_w(u8 data);
	u8 icrh_r(); u8 icrl_r();
	u8 tcsr2_r(); void tcsr2_w(u8 data);
	u8 ocr2h_r(); u8 ocr2l_r(); void ocr2h_w(u8 data); void ocr2l_w(u8 data);
	void modified_counters();
	void set_timer_event();
	void check_timer_event();

	// timer 2 (hd6301x's dedicated sub-timer, used for TOUT3 / SCI baud gen; 0x1b-0x1d)
	u8 m_t2cnt = 0, m_tconr = 0xff, m_tcsr3 = 0;
	bool m_tout3 = false, m_t2cnt_written = false;
	u8 t2cnt_r(); void t2cnt_w(u8 data);
	void tconr_w(u8 data);
	u8 tcsr3_r(); void tcsr3_w(u8 data);
	void increment_t2cnt(int amount);

	// SCI / UART (0x10-0x13)
	u8 m_trcsr = 0, m_rmcr = 0, m_rdr = 0, m_tdr = 0, m_rsr = 0, m_tshr = 0;
	int m_rxbits = 0, m_txbits = 0, m_txstate = 0, m_tx = 1;
	int m_trcsr_read_tdre = 0, m_trcsr_read_orfe = 0, m_trcsr_read_rdrf = 0;
	int m_sci_clocks = 0;
	bool m_use_ext_serclock = false;
	int m_sclk_divider = 16;

	u8 sci_rmcr_r(); void sci_rmcr_w(u8 data);
	u8 sci_trcsr_r(); void sci_trcsr_w(u8 data);
	u8 sci_rdr_r(); void sci_tdr_w(u8 data);
	void set_rmcr(u8 data);
	int m6801_rx();
	void serial_transmit();
	void serial_receive();
	void sci_tick();
	void sci_clock_internal(u8 divider);
	void reset_sci_timer();

	// RAM control register (0x14) - bit0 gates IRQ1 recognition, matching
	// hd6301x_cpu_device::check_irq1_enabled() in MAME
	u8 m_ram_ctrl = 0;
	u8 rcr_r(); void rcr_w(u8 data);

	u8 ff_r() { return 0xff; }

	u8 internal_read(u16 addr);
	void internal_write(u16 addr, u8 data);

	// --- opcode handlers (bodies live in 6800ops.hxx, copied verbatim from MAME) ---
	void aba(); void abx(); void adca_di(); void adca_ex(); void adca_im(); void adca_ix();
	void adcb_di(); void adcb_ex(); void adcb_im(); void adcb_ix(); void adcx_im();
	void adda_di(); void adda_ex(); void adda_im(); void adda_ix();
	void addb_di(); void addb_ex(); void addb_im(); void addb_ix();
	void addd_di(); void addd_ex(); void addx_ex(); void addd_im(); void addd_ix();
	void aim_di(); void aim_ix();
	void anda_di(); void anda_ex(); void anda_im(); void anda_ix();
	void andb_di(); void andb_ex(); void andb_im(); void andb_ix();
	void asl_ex(); void asl_ix(); void asla(); void aslb(); void asld();
	void asr_ex(); void asr_ix(); void asra(); void asrb();
	void bcc(); void bcs(); void beq(); void bge(); void bgt(); void bhi();
	void bita_di(); void bita_ex(); void bita_im(); void bita_ix();
	void bitb_di(); void bitb_ex(); void bitb_im(); void bitb_ix();
	void ble(); void bls(); void blt(); void bmi(); void bne(); void bpl(); void bra(); void brn();
	void bsr(); void bvc(); void bvs();
	void cba(); void clc(); void cli(); void clr_ex(); void clr_ix(); void clra(); void clrb(); void clv();
	void cmpa_di(); void cmpa_ex(); void cmpa_im(); void cmpa_ix();
	void cmpb_di(); void cmpb_ex(); void cmpb_im(); void cmpb_ix();
	void cmpx_di(); void cmpx_ex(); void cmpx_im(); void cmpx_ix();
	void com_ex(); void com_ix(); void coma(); void comb();
	void daa();
	void dec_ex(); void dec_ix(); void deca(); void decb(); void des(); void dex();
	void eim_di(); void eim_ix();
	void eora_di(); void eora_ex(); void eora_im(); void eora_ix();
	void eorb_di(); void eorb_ex(); void eorb_im(); void eorb_ix();
	void illegl1(); void illegl2(); void illegl3();
	void inc_ex(); void inc_ix(); void inca(); void incb(); void ins(); void inx();
	void jmp_ex(); void jmp_ix(); void jsr_di(); void jsr_ex(); void jsr_ix();
	void lda_di(); void lda_ex(); void lda_im(); void lda_ix();
	void ldb_di(); void ldb_ex(); void ldb_im(); void ldb_ix();
	void ldd_di(); void ldd_ex(); void ldd_im(); void ldd_ix();
	void lds_di(); void lds_ex(); void lds_im(); void lds_ix();
	void ldx_di(); void ldx_ex(); void ldx_im(); void ldx_ix();
	void lsr_ex(); void lsr_ix(); void lsra(); void lsrb(); void lsrd();
	void mul();
	void neg_ex(); void neg_ix(); void nega(); void negb(); void nop();
	void oim_di(); void oim_ix();
	void ora_di(); void ora_ex(); void ora_im(); void ora_ix();
	void orb_di(); void orb_ex(); void orb_im(); void orb_ix();
	void psha(); void pshb(); void pshx(); void pula(); void pulb(); void pulx();
	void rol_ex(); void rol_ix(); void rola(); void rolb();
	void ror_ex(); void ror_ix(); void rora(); void rorb();
	void rti(); void rts();
	void sba();
	void sbca_di(); void sbca_ex(); void sbca_im(); void sbca_ix();
	void sbcb_di(); void sbcb_ex(); void sbcb_im(); void sbcb_ix();
	void sec(); void sei(); void sev(); void slp();
	void sta_di(); void sta_ex(); void sta_im(); void sta_ix();
	void stb_di(); void stb_ex(); void stb_im(); void stb_ix();
	void std_di(); void std_ex(); void std_im(); void std_ix();
	void sts_di(); void sts_ex(); void sts_im(); void sts_ix();
	void stx_di(); void stx_ex(); void stx_im(); void stx_ix();
	void suba_di(); void suba_ex(); void suba_im(); void suba_ix();
	void subb_di(); void subb_ex(); void subb_im(); void subb_ix();
	void subd_di(); void subd_ex(); void subd_im(); void subd_ix();
	void swi();
	void tab(); void tap(); void tba(); void tim_di(); void tim_ix(); void tpa();
	void tst_ex(); void tst_ix(); void tsta(); void tstb(); void tsx(); void txs();
	void undoc1(); void undoc2();
	void wai(); void xgdx();
	void cpx_di(); void cpx_ex(); void cpx_im(); void cpx_ix();
	void trap();
	void btst_ix(); void stx_nsc();
};
