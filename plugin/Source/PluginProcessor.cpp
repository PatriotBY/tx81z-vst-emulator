#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>

namespace {
constexpr int kRomBytes = 0x10000;
constexpr int kNvramBytes = 0x2000;
}

Tx81zAudioProcessor::Tx81zAudioProcessor()
	: AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
	, cpu(bus)
{
	bus.irq1_callback = [this](bool asserted) { cpu.set_irq1(asserted); };
	cpu.port6_output = [this](tx81z::u8 data, tx81z::u8 /*ddr*/)
	{
		bus.set_rom_bank((data >> 3) & 1);
		ledBitsAtomic.store((data >> 4) & 0x0f);
	};

	// P2: bit3 = MIDI In (rxLine), bit5 = Cursor, bit6 = Master Volume Up,
	// bit7 = Master Volume Down (all active-low, see ymtx81z.cpp INPUT_PORTS)
	cpu.port2_input = [this]() -> tx81z::u8
	{
		tx81z::u8 v = rxLine ? 0xff : 0xf7;
		if (buttons.cursor.load()) v &= ~0x20;
		if (buttons.masterVolUp.load()) v &= ~0x40;
		if (buttons.masterVolDown.load()) v &= ~0x80;
		return v;
	};
	// P5: bit1 = Store/Eq Copy, bit4 = Data Entry Inc, bit5 = Data Entry Dec,
	// bit6 = Voice Parameter ->, bit7 = Voice Parameter <-
	cpu.port5_input = [this]() -> tx81z::u8
	{
		tx81z::u8 v = 0xff;
		if (buttons.storeEqCopy.load()) v &= ~0x02;
		if (buttons.dataEntryInc.load()) v &= ~0x10;
		if (buttons.dataEntryDec.load()) v &= ~0x20;
		if (buttons.paramRight.load()) v &= ~0x40;
		if (buttons.paramLeft.load()) v &= ~0x80;
		return v;
	};
	// P6: bit0 = Play/Perform, bit1 = Edit/Compare, bit2 = Utility
	cpu.port6_input = [this]() -> tx81z::u8
	{
		tx81z::u8 v = 0xff;
		if (buttons.playPerform.load()) v &= ~0x01;
		if (buttons.editCompare.load()) v &= ~0x02;
		if (buttons.utility.load()) v &= ~0x04;
		return v;
	};

	loadResources();

	// Real hardware's NVRAM just retains state as long as it has battery
	// power - there's no explicit "save" button. Polling every few seconds
	// and writing back whenever something changed is the closest equivalent
	// for a plugin: it survives closing/reopening the DAW, same as picking
	// up a real unit that was never turned off.
	startTimer(3000);
}

Tx81zAudioProcessor::~Tx81zAudioProcessor()
{
	stopTimer();
	flushNvramIfDirty(); // final save so nothing done just before closing is lost
}

void Tx81zAudioProcessor::timerCallback()
{
	flushNvramIfDirty();
}

void Tx81zAudioProcessor::flushNvramIfDirty()
{
	if (!nvramLoaded || !bus.nvram_dirty() || !nvramFilePath.existsAsFile())
		return;

	auto snapshot = bus.nvram_snapshot();
	if (nvramFilePath.replaceWithData(snapshot.data(), snapshot.size()))
		bus.clear_nvram_dirty();
	// if the write failed (e.g. file locked/read-only), just try again next
	// tick - nvram_dirty() stays set, nothing is lost in the meantime since
	// the live state is still sitting in the bus's own memory
}

void Tx81zAudioProcessor::getStateInformation(juce::MemoryBlock &destData)
{
	if (!nvramLoaded)
		return;
	auto snapshot = bus.nvram_snapshot();
	destData.append(snapshot.data(), snapshot.size());
}

void Tx81zAudioProcessor::setStateInformation(const void *data, int sizeInBytes)
{
	if (!nvramLoaded || sizeInBytes != kNvramBytes)
		return;
	std::vector<tx81z::u8> nvram(kNvramBytes);
	std::memcpy(nvram.data(), data, kNvramBytes);
	try
	{
		bus.load_nvram(nvram);
	}
	catch (const std::exception &)
	{
		// size already checked above, shouldn't happen - ignore defensively
	}
}

void Tx81zAudioProcessor::loadResources()
{
	// Real TX81Z firmware/NVRAM dumps - copyrighted/personal, never bundled
	// into the plugin binary or committed to any repo. Same handling as the
	// D-110 project: the user drops the files once into a fixed local
	// folder, matched by size rather than filename.
	auto dataDir = juce::File("C:/Program Files/Common Files/VST3/TX81Z Data");

	if (!dataDir.isDirectory())
	{
		resourceStatusMessage = "Data folder not found: " + dataDir.getFullPathName();
		return;
	}

	juce::File romFile, nvramFile;
	for (const auto &entry : juce::RangedDirectoryIterator(dataDir, false, "*", juce::File::findFiles))
	{
		auto f = entry.getFile();
		auto size = f.getSize();
		if (size == kRomBytes && !romFile.existsAsFile())
			romFile = f;
		else if (size == kNvramBytes && !nvramFile.existsAsFile())
			nvramFile = f;
	}

	if (!romFile.existsAsFile())
	{
		resourceStatusMessage = "No 64KB ROM file found in " + dataDir.getFullPathName();
		return;
	}
	if (!nvramFile.existsAsFile())
	{
		resourceStatusMessage = "No 8KB NVRAM file found in " + dataDir.getFullPathName();
		return;
	}

	std::vector<tx81z::u8> rom(kRomBytes), nvram(kNvramBytes);

	{
		juce::MemoryBlock block;
		romFile.loadFileAsData(block);
		if (block.getSize() != (size_t)kRomBytes)
		{
			resourceStatusMessage = "ROM file " + romFile.getFileName() + " is not exactly 64KB";
			return;
		}
		std::memcpy(rom.data(), block.getData(), kRomBytes);
	}
	{
		juce::MemoryBlock block;
		nvramFile.loadFileAsData(block);
		if (block.getSize() != (size_t)kNvramBytes)
		{
			resourceStatusMessage = "NVRAM file " + nvramFile.getFileName() + " is not exactly 8KB";
			return;
		}
		std::memcpy(nvram.data(), block.getData(), kNvramBytes);
	}

	try
	{
		bus.load_rom(rom);
		bus.load_nvram(nvram);
		romLoaded = true;
		nvramLoaded = true;
		nvramFilePath = nvramFile;
		resourceStatusMessage = "Loaded " + romFile.getFileName() + " + " + nvramFile.getFileName();
	}
	catch (const std::exception &e)
	{
		resourceStatusMessage = juce::String("Failed to load: ") + e.what();
	}
}

void Tx81zAudioProcessor::togglePower()
{
	bool nowOn = !powerOn.load();
	powerOn.store(nowOn);
	if (nowOn)
	{
		// Consumed at the top of the next processBlock (audio thread) -
		// resets the CPU there rather than here, since cpu/bus are only ever
		// touched from the audio thread. Booting is then just the normal
		// per-sample loop running from a freshly-reset CPU in real time, same
		// as real hardware - no fast-forward, so the boot banner actually
		// plays out over real elapsed seconds instead of being skipped
		// before it's ever visible.
		pendingBootReset.store(true);
	}
	else
	{
		ledBitsAtomic.store(0);
		const juce::ScopedLock sl(lcdLock);
		lcdLine1.clear();
		lcdLine2.clear();
	}
}

void Tx81zAudioProcessor::prepareToPlay(double sampleRate, int)
{
	hostSampleRate = sampleRate;
	chipSampleRate = bus.opz_sample_rate(uint32_t(CPU_CLOCK_HZ / 2.0));
}

bool Tx81zAudioProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
	return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void Tx81zAudioProcessor::onSciTick()
{
	if (!sendingByte)
	{
		if (pendingByteIndex >= pendingMidiBytes.size())
		{
			rxLine = true; // idle / mark
			return;
		}

		uint8_t byte = pendingMidiBytes[pendingByteIndex++];
		if (pendingByteIndex >= pendingMidiBytes.size())
		{
			pendingMidiBytes.clear();
			pendingByteIndex = 0;
		}

		currentBits[0] = false; // start bit
		for (int i = 0; i < 8; ++i)
			currentBits[1 + i] = (byte >> i) & 1;
		currentBits[9] = true; // stop bit

		bitPos = 0;
		ticksIntoBit = 0;
		sendingByte = true;
		rxLine = currentBits[0];
		return;
	}

	if (++ticksIntoBit >= 16)
	{
		ticksIntoBit = 0;
		++bitPos;
		if (bitPos >= 10)
		{
			sendingByte = false;
			rxLine = true;
		}
		else
		{
			rxLine = currentBits[bitPos];
		}
	}
}

void Tx81zAudioProcessor::stepOneSample()
{
	// Auto-press Play/Perform ~1.8s after a real factory NVRAM's cold-start
	// "UTILITY MODE" screen appears, same timing bootFirmware() used to
	// fast-forward through - now it plays out over real wall-clock seconds
	// instead, timed against the same clock the CPU/MIDI/audio advance on.
	bootElapsedSeconds += 1.0 / hostSampleRate;
	if (!autoPlayPerformPressed && bootElapsedSeconds >= 1.8)
	{
		buttons.playPerform.store(true);
		autoPlayPerformPressed = true;
	}
	else if (autoPlayPerformPressed && !autoPlayPerformReleased && bootElapsedSeconds >= 2.0)
	{
		buttons.playPerform.store(false);
		autoPlayPerformReleased = true;
	}

	cpuCyclesAccum += CPU_CLOCK_HZ / hostSampleRate;
	while (cpuCyclesAccum >= 1.0)
	{
		int chunk = std::min(200, int(cpuCyclesAccum));
		if (chunk <= 0)
			chunk = 1;
		int actual = cpu.run(chunk);
		cpuCyclesAccum -= actual;

		midiClockAccum += actual * (MIDI_CLOCK_HZ / CPU_CLOCK_HZ);
		while (midiClockAccum >= 1.0)
		{
			cpu.clock_serial();
			midiClockAccum -= 1.0;
			onSciTick();
		}
	}

	chipSampleAccum += chipSampleRate / hostSampleRate;
	while (chipSampleAccum >= 1.0)
	{
		ymfm::ym2414::output_data out;
		bus.generate_audio(&out);
		out.clamp16();
		lastL = out.data[0];
		lastR = out.data[1];
		chipSampleAccum -= 1.0;
	}

	// Refresh the GUI's LCD snapshot at ~20Hz rather than every sample -
	// plenty for a human-readable display, far cheaper than locking per-sample.
	if (++samplesSinceLcdUpdate >= int(hostSampleRate / 20.0))
	{
		samplesSinceLcdUpdate = 0;
		std::string text = bus.lcd().display_text(2, 16);
		auto splitAt = text.find('\n');
		const juce::ScopedLock sl(lcdLock);
		if (splitAt != std::string::npos)
		{
			lcdLine1 = juce::String(text.substr(0, splitAt));
			lcdLine2 = juce::String(text.substr(splitAt + 1));
		}
		else
		{
			lcdLine1 = juce::String(text);
			lcdLine2.clear();
		}
	}
}

void Tx81zAudioProcessor::getLcdText(juce::String &line1, juce::String &line2) const
{
	const juce::ScopedLock sl(lcdLock);
	line1 = lcdLine1;
	line2 = lcdLine2;
}

void Tx81zAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midiMessages)
{
	juce::ScopedNoDenormals noDenormals;
	buffer.clear();

	if (!isReady() || !powerOn.load())
		return; // powered off (or not loaded yet): frozen and silent, same as no power reaching the CPU

	if (pendingBootReset.exchange(false))
	{
		cpu.reset();
		cpuCyclesAccum = midiClockAccum = chipSampleAccum = 0.0;
		pendingMidiBytes.clear();
		pendingByteIndex = 0;
		sendingByte = false;
		rxLine = true;
		samplesSinceLcdUpdate = 0;
		bootElapsedSeconds = 0.0;
		autoPlayPerformPressed = autoPlayPerformReleased = false;
	}

	struct QueuedEvent { int pos; std::vector<uint8_t> bytes; };
	std::vector<QueuedEvent> events;
	for (const auto metadata : midiMessages)
	{
		events.push_back({ metadata.samplePosition, std::vector<uint8_t>(metadata.data, metadata.data + metadata.numBytes) });
	}

	auto *left = buffer.getWritePointer(0);
	auto *right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
	int numSamples = buffer.getNumSamples();

	size_t evIdx = 0;
	for (int i = 0; i < numSamples; ++i)
	{
		while (evIdx < events.size() && events[evIdx].pos == i)
		{
			pendingMidiBytes.insert(pendingMidiBytes.end(), events[evIdx].bytes.begin(), events[evIdx].bytes.end());
			++evIdx;
		}

		stepOneSample();

		left[i] = float(lastL / 32768.0 * 0.60);
		if (right)
			right[i] = float(lastR / 32768.0 * 0.60);
	}
}

juce::AudioProcessorEditor *Tx81zAudioProcessor::createEditor()
{
	return new Tx81zAudioProcessorEditor(*this);
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter()
{
	return new Tx81zAudioProcessor();
}
