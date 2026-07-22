#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "tx81z/cpu_core.h"
#include "tx81z/tx81z_bus.h"

#include <atomic>
#include <vector>

// Front-panel button state - written by the GUI (message thread) on mouse
// down/up, read by the audio thread inside port2/5/6_input. Matches the real
// button-to-port mapping in src/mame/yamaha/ymtx81z.cpp's INPUT_PORTS block:
// P2 = Cursor/Master Volume, P5 = Store-EqCopy/Data Entry/Voice Parameter,
// P6 = Play-Perform/Edit-Compare/Utility.
struct Tx81zButtonState
{
	std::atomic<bool> cursor{ false };
	std::atomic<bool> masterVolUp{ false };
	std::atomic<bool> masterVolDown{ false };
	std::atomic<bool> storeEqCopy{ false };
	std::atomic<bool> dataEntryInc{ false };
	std::atomic<bool> dataEntryDec{ false };
	std::atomic<bool> paramRight{ false };
	std::atomic<bool> paramLeft{ false };
	std::atomic<bool> playPerform{ false };
	std::atomic<bool> editCompare{ false };
	std::atomic<bool> utility{ false };
};

class Tx81zAudioProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
	Tx81zAudioProcessor();
	~Tx81zAudioProcessor() override;

	void prepareToPlay(double sampleRate, int samplesPerBlock) override;
	void releaseResources() override {}
	bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
	void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

	juce::AudioProcessorEditor *createEditor() override;
	bool hasEditor() const override { return true; }

	const juce::String getName() const override { return "TX81Z Emulator"; }
	bool acceptsMidi() const override { return true; }
	bool producesMidi() const override { return false; }
	bool isMidiEffect() const override { return false; }
	double getTailLengthSeconds() const override { return 2.0; }

	int getNumPrograms() override { return 1; }
	int getCurrentProgram() override { return 0; }
	void setCurrentProgram(int) override {}
	const juce::String getProgramName(int) override { return {}; }
	void changeProgramName(int, const juce::String &) override {}

	// Embeds the current NVRAM snapshot into the DAW's own project save, as a
	// second line of defense on top of the external-file persistence below -
	// covers "sent someone else the project file" or "external file got
	// deleted" cases.
	void getStateInformation(juce::MemoryBlock &) override;
	void setStateInformation(const void *, int) override;

	// true once both the ROM and NVRAM were found and loaded successfully
	bool isReady() const { return romLoaded && nvramLoaded; }
	const juce::String &resourceStatus() const { return resourceStatusMessage; }

	// GUI writes button presses here; read by the audio thread's port
	// callbacks. Public and directly mutable by design - same spirit as a
	// physical switch, no need for a message-passing layer.
	Tx81zButtonState buttons;

	// Power switch - a real latching toggle, not a momentary press. While
	// off, the engine stops advancing (frozen, same as no power reaching the
	// CPU) and the audio output is silent.
	std::atomic<bool> powerOn{ true };

	// LEDs 1-4 (port6 bits 4-7, see cpu.port6_output below), bit0=led1.
	int ledBits() const { return ledBitsAtomic.load(); }

	// Thread-safe snapshot of the 2x16 LCD text for the GUI to poll/repaint.
	void getLcdText(juce::String &line1, juce::String &line2) const;

private:
	static constexpr double CPU_CLOCK_HZ = 7159090.0;   // 7.15909 MHz XTAL
	static constexpr double MIDI_CLOCK_HZ = 500000.0;   // separate 500kHz XTAL for the SCI

	tx81z::Tx81zBus bus;
	m6800_cpu_device cpu;
	bool romLoaded = false, nvramLoaded = false;
	juce::String resourceStatusMessage;
	juce::File nvramFilePath; // where loadResources() found the nvram dump - flushNvramIfDirty() writes back here

	void timerCallback() override; // periodic NVRAM flush - see flushNvramIfDirty()
	void flushNvramIfDirty();

	double hostSampleRate = 44100.0;
	double chipSampleRate = 55930.0;
	double cpuCyclesAccum = 0.0;
	double midiClockAccum = 0.0;
	double chipSampleAccum = 0.0;
	double lastL = 0.0, lastR = 0.0;

	// MIDI UART bit-banging state (feeds incoming JUCE MIDI messages into the
	// CPU's SCI RX pin, one bit at a time, at the real 31250 baud rate - see
	// engine/tools/render_note.cpp's MidiFeeder for the console-tool version
	// of this exact same mechanism).
	std::vector<uint8_t> pendingMidiBytes;
	size_t pendingByteIndex = 0;
	bool sendingByte = false;
	bool currentBits[10] = {};
	int bitPos = 0;
	int ticksIntoBit = 0;
	bool rxLine = true;

	std::atomic<int> ledBitsAtomic{ 0 };
	juce::CriticalSection lcdLock;
	juce::String lcdLine1, lcdLine2;
	int samplesSinceLcdUpdate = 0;

	void loadResources();
	void bootFirmware();
	void stepOneSample();
	void onSciTick();

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Tx81zAudioProcessor)
};
