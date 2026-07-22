// Diagnostic tool: boots real firmware, sends a real MIDI Note On (bit-banged
// over the SCI at genuine 31250 baud, exactly like a real MIDI cable would),
// lets the note ring while the OPZ chip generates audio, and writes the
// result to a WAV file so a human can actually listen to it.
#include "tx81z/cpu_core.h"
#include "tx81z/tx81z_bus.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <vector>

namespace {

constexpr double CPU_CLOCK_HZ = 7159090.0;    // 7.15909 MHz XTAL
constexpr double MIDI_CLOCK_HZ = 500000.0;    // separate 500kHz XTAL for the SCI
constexpr double OPZ_CLOCK_HZ = CPU_CLOCK_HZ / 2.0; // ymsnd is wired to XTAL/2

std::vector<tx81z::u8> load_file(const char *path)
{
	std::ifstream f(path, std::ios::binary);
	if (!f)
		throw std::runtime_error(std::string("cannot open ") + path);
	return std::vector<tx81z::u8>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void write_wav(const char *path, const std::vector<int16_t> &interleaved_stereo, uint32_t sample_rate)
{
	std::ofstream f(path, std::ios::binary);
	uint32_t data_bytes = uint32_t(interleaved_stereo.size() * sizeof(int16_t));
	uint32_t byte_rate = sample_rate * 2 * 2;
	uint16_t block_align = 4;
	uint32_t riff_size = 36 + data_bytes;

	f.write("RIFF", 4);
	f.write(reinterpret_cast<const char *>(&riff_size), 4);
	f.write("WAVE", 4);
	f.write("fmt ", 4);
	uint32_t fmt_size = 16;
	f.write(reinterpret_cast<const char *>(&fmt_size), 4);
	uint16_t audio_format = 1, num_channels = 2;
	uint16_t bits_per_sample = 16;
	f.write(reinterpret_cast<const char *>(&audio_format), 2);
	f.write(reinterpret_cast<const char *>(&num_channels), 2);
	f.write(reinterpret_cast<const char *>(&sample_rate), 4);
	f.write(reinterpret_cast<const char *>(&byte_rate), 4);
	f.write(reinterpret_cast<const char *>(&block_align), 2);
	f.write(reinterpret_cast<const char *>(&bits_per_sample), 2);
	f.write("data", 4);
	f.write(reinterpret_cast<const char *>(&data_bytes), 4);
	f.write(reinterpret_cast<const char *>(interleaved_stereo.data()), data_bytes);
}

// Bit-bangs one MIDI byte over the SCI's RX line at the real 31250 baud rate
// (500kHz external clock / 16, same division the CPU's SCI uses - see
// hd6301x's m_sclk_divider), interleaved with normal CPU execution so
// firmware keeps running while the byte arrives, exactly like real hardware.
class MidiFeeder
{
public:
	// advance_one_bit_period must run the CPU for exactly one UART bit
	// period's worth of CPU cycles (16 SCI clock ticks - see
	// hd6301x's m_sclk_divider - which in CPU-cycle terms is
	// 16 * CPU_CLOCK_HZ / MIDI_CLOCK_HZ, NOT literally "16 of anything CPU
	// cycles"; passing the wrong unit here silently desyncs the bit timing
	// and the CPU just samples idle-1 the whole time, reconstructing 0xFF).
	void send(tx81z::u8 byte, const std::function<void()> &advance_one_bit_period)
	{
		bool bits[10];
		bits[0] = false; // start bit
		for (int i = 0; i < 8; ++i)
			bits[1 + i] = (byte >> i) & 1; // LSB first
		bits[9] = true; // stop bit

		for (bool bit : bits)
		{
			m_rx = bit;
			advance_one_bit_period();
		}
	}

	bool rx_bit() const { return m_rx; }

private:
	bool m_rx = true; // idle = mark = 1
};

} // namespace

int main(int argc, char **argv)
{
	const char *rom_path = (argc > 1) ? argv[1] : "roms/tx81z-v1.6.ic15";
	const char *out_path = (argc > 2) ? argv[2] : "note.wav";
	int midi_note = (argc > 3) ? std::atoi(argv[3]) : 60;
	int midi_velocity = (argc > 4) ? std::atoi(argv[4]) : 100;
	const char *nvram_path = (argc > 5) ? argv[5] : nullptr;

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

	if (nvram_path)
	{
		std::vector<tx81z::u8> nvram;
		try
		{
			nvram = load_file(nvram_path);
			bus.load_nvram(nvram);
			std::printf("Loaded real NVRAM dump: %s (%zu bytes)\n", nvram_path, nvram.size());
		}
		catch (const std::exception &e)
		{
			std::fprintf(stderr, "Failed to load NVRAM: %s\n", e.what());
			return 1;
		}
	}

	m6800_cpu_device cpu(bus);
	bus.irq1_callback = [&cpu](bool asserted) { cpu.set_irq1(asserted); };

	int leds = 0;
	cpu.port6_output = [&](tx81z::u8 data, tx81z::u8 /*ddr*/)
	{
		bus.set_rom_bank((data >> 3) & 1);
		leds = (data >> 4) & 0x0f;
	};

	MidiFeeder midi;
	cpu.port2_input = [&midi]() -> tx81z::u8
	{
		// bit3 = MIDI In; everything else idle-high (unused buttons/lines)
		return midi.rx_bit() ? 0xff : 0xf7;
	};
	bool store_button_pressed = false;
	cpu.port5_input = [&]() -> tx81z::u8
	{
		// P5 bit1 = Store/Eq Copy button, active-low
		return store_button_pressed ? tx81z::u8(0xfd) : tx81z::u8(0xff);
	};
	bool play_button_pressed = false;
	cpu.port6_input = [&]() -> tx81z::u8
	{
		// P6 bit0 = Play/Perform button, active-low
		return play_button_pressed ? tx81z::u8(0xfe) : tx81z::u8(0xff);
	};

	int sci_bytes_seen = 0;
	cpu.on_sci_byte_received = [&](tx81z::u8 b)
	{
		++sci_bytes_seen;
		std::printf("  [SCI RX] byte #%d = 0x%02X\n", sci_bytes_seen, b);
	};

	cpu.on_sci_error = [](int code)
	{
		std::printf("  [SCI ERROR] %s\n", code == 1 ? "overrun (RDRF still set)" : "framing error");
	};

	int opz_writes_seen = 0;
	bool log_opz_writes = false;
	std::vector<std::pair<tx81z::u8, tx81z::u8>> opz_log; // (register, value) - data writes only
	tx81z::u8 last_opz_addr = 0;
	bus.opz_write_debug = [&](tx81z::u8 offset, tx81z::u8 data)
	{
		++opz_writes_seen;
		if (offset == 0)
			last_opz_addr = data;
		else if (log_opz_writes)
			opz_log.emplace_back(last_opz_addr, data);
	};

	cpu.reset();

	double midi_clock_accum = 0.0;
	double sample_accum = 0.0;
	const double opz_sample_rate = bus.opz_sample_rate(uint32_t(OPZ_CLOCK_HZ));
	std::printf("OPZ native sample rate: %.1f Hz\n", opz_sample_rate);

	std::vector<int16_t> pcm; // interleaved stereo

	// Advances the CPU (and, in step, the MIDI clock + audio generation) by
	// exactly `cycles` CPU cycles - the single place all three clock domains
	// (7.15909MHz CPU, 500kHz SCI, ~55930Hz OPZ audio) get kept in lockstep.
	auto advance = [&](int cycles)
	{
		int actual = cpu.run(cycles);

		midi_clock_accum += actual * (MIDI_CLOCK_HZ / CPU_CLOCK_HZ);
		while (midi_clock_accum >= 1.0)
		{
			cpu.clock_serial();
			midi_clock_accum -= 1.0;
		}

		sample_accum += actual * (opz_sample_rate / CPU_CLOCK_HZ);
		while (sample_accum >= 1.0)
		{
			ymfm::ym2414::output_data out;
			bus.generate_audio(&out);
			out.clamp16();
			// ymtx81z.cpp mixes both channels to the speaker at 0.60 volume
			pcm.push_back(int16_t(out.data[0] * 0.60));
			pcm.push_back(int16_t(out.data[1] * 0.60));
			sample_accum -= 1.0;
		}
	};

	auto run_seconds = [&](double seconds)
	{
		long long total = (long long)(CPU_CLOCK_HZ * seconds);
		long long done = 0;
		while (done < total)
		{
			advance(200);
			done += 200;
		}
	};

	// one UART bit period = 16 SCI clock ticks (hd6301x's m_sclk_divider),
	// converted into CPU cycles via the two crystals' ratio
	const int cycles_per_bit = (int)std::lround(16.0 * CPU_CLOCK_HZ / MIDI_CLOCK_HZ);
	auto advance_one_bit = [&]() { advance(cycles_per_bit); };

	std::printf("Booting firmware...\n");
	run_seconds(1.8); // a bit more than the 1.5s it takes the boot banner to finish typing

	std::printf("LCD after boot:\n%s\n", bus.lcd().display_text(2, 16).c_str());

	// With a blank NVRAM, firmware drops into a "UTILITY MODE / Master Tune"
	// screen a couple seconds after boot (a dead-battery cold-start behavior -
	// confirmed unrelated to MIDI, happens with no button input at all) and
	// never actually keys a note on afterward, even in Play mode - real
	// instrument/patch tables live in that same blank NVRAM. Pressing
	// Play/Perform here is a harmless no-op if a real NVRAM dump already put
	// us in Play mode; it's the fix for the blank-NVRAM case.
	play_button_pressed = true;
	run_seconds(0.2);
	play_button_pressed = false;
	run_seconds(1.0); // let any button-scan/LCD-update activity fully settle before MIDI

	std::printf("LCD after pressing Play/Perform:\n%s\n", bus.lcd().display_text(2, 16).c_str());

	std::printf("Sending Note On: note=%d velocity=%d\n", midi_note, midi_velocity);
	log_opz_writes = true;
	midi.send(0x90, advance_one_bit); // Note On, channel 1
	midi.send(tx81z::u8(midi_note), advance_one_bit);
	midi.send(tx81z::u8(midi_velocity), advance_one_bit);

	run_seconds(2.0); // let the note ring
	log_opz_writes = false;

	std::printf("Sending Note Off\n");
	midi.send(0x80, advance_one_bit); // Note Off, channel 1
	midi.send(tx81z::u8(midi_note), advance_one_bit);
	midi.send(tx81z::u8(64), advance_one_bit);

	run_seconds(1.0); // let the release tail finish

	std::printf("Total SCI bytes received: %d, total OPZ register writes: %d\n", sci_bytes_seen, opz_writes_seen);
	std::printf("OPZ writes during Note On + 2s ring (%zu total, showing key/algorithm/freq registers 0x20-0x2F and first/last 10):\n", opz_log.size());
	for (size_t i = 0; i < opz_log.size(); ++i)
	{
		tx81z::u8 reg = opz_log[i].first, val = opz_log[i].second;
		bool interesting = (reg >= 0x20 && reg <= 0x2f) || i < 10 || i + 10 >= opz_log.size();
		if (interesting)
			std::printf("  [%zu] reg 0x%02X = 0x%02X\n", i, reg, val);
	}

	write_wav(out_path, pcm, uint32_t(opz_sample_rate));
	std::printf("Wrote %s (%zu samples, %.2fs)\n", out_path, pcm.size() / 2, pcm.size() / 2.0 / opz_sample_rate);

	// quick sanity readout: is there actually any signal, or is it silent?
	int64_t sum_abs = 0;
	int16_t peak = 0;
	for (int16_t s : pcm)
	{
		sum_abs += std::abs(int(s));
		if (std::abs(int(s)) > peak) peak = int16_t(std::abs(int(s)));
	}
	double mean_abs = pcm.empty() ? 0.0 : double(sum_abs) / pcm.size();
	std::printf("Peak sample: %d / 32767   Mean |sample|: %.1f\n", peak, mean_abs);

	return 0;
}
