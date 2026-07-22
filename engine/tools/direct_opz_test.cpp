// Isolates the OPZ/ymfm audio pipeline from firmware entirely: pokes the chip
// directly through the same Bus the CPU uses, with a hand-crafted "algorithm 7
// (all 4 operators in parallel), moderate volume, fast attack, full sustain"
// patch on channel 0, then renders straight to WAV. If this produces sound but
// feeding real firmware doesn't, the problem is upstream of the chip (firmware
// logic / blank-NVRAM cold-start state), not the ymfm integration itself.
#include "tx81z/tx81z_bus.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

namespace {

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
	uint16_t audio_format = 1, num_channels = 2, bits_per_sample = 16;
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

} // namespace

int main()
{
	tx81z::Tx81zBus bus;

	auto opz_write = [&](tx81z::u8 reg, tx81z::u8 data)
	{
		bus.write(0x2000, reg);  // address port
		bus.write(0x2001, data); // data port
	};

	// Channel 0, all 4 operator slots (opnum = channel | (slot << 3) -> 0,8,16,24)
	for (int slot = 0; slot < 4; ++slot)
	{
		tx81z::u8 op = tx81z::u8(0 + slot * 8);
		opz_write(0x40 + op, 0x01); // detune=0, multiple=1
		opz_write(0x60 + op, 0x10); // total level = 16 (fairly loud, 0=loudest/127=silent)
		opz_write(0x80 + op, 0x1F); // KSR=0, attack rate=31 (fastest)
		opz_write(0xA0 + op, 0x00); // decay rate = 0 (no decay - straight to sustain)
		opz_write(0xC0 + op, 0x00); // detune2=0, sustain rate=0
		opz_write(0xE0 + op, 0x08); // sustain level=0 (full), release rate=8
	}

	opz_write(0x28, 0x1D); // key code (same value real firmware used for note 60)
	opz_write(0x30, 0x01); // key fraction / mono bit
	opz_write(0x38, 0x00); // LFO sensitivity off

	opz_write(0x20, 0xC7); // pan=stereo(1), key ON (bit6=0), feedback=0, algorithm=7 (all parallel)

	const double opz_clock = 7159090.0 / 2.0;
	const double sample_rate = bus.opz_sample_rate(uint32_t(opz_clock));
	std::printf("OPZ native sample rate: %.1f Hz\n", sample_rate);

	std::vector<int16_t> pcm;
	int total_samples = int(sample_rate * 1.5); // 1.5s
	for (int i = 0; i < total_samples; ++i)
	{
		ymfm::ym2414::output_data out;
		bus.generate_audio(&out);
		out.clamp16();
		pcm.push_back(int16_t(out.data[0] * 0.60));
		pcm.push_back(int16_t(out.data[1] * 0.60));

		if (i == total_samples / 2)
		{
			opz_write(0x20, 0xE7); // key OFF (bit6=1) halfway through, to check release too
		}
	}

	write_wav("direct_opz_test.wav", pcm, uint32_t(sample_rate));

	int64_t sum_abs = 0;
	int16_t peak = 0;
	for (int16_t s : pcm)
	{
		sum_abs += std::abs(int(s));
		if (std::abs(int(s)) > peak) peak = int16_t(std::abs(int(s)));
	}
	std::printf("Wrote direct_opz_test.wav (%d samples). Peak: %d / 32767   Mean |sample|: %.1f\n",
		total_samples, peak, pcm.empty() ? 0.0 : double(sum_abs) / pcm.size());

	return 0;
}
