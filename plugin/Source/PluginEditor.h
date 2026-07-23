#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// A momentary, active-low style panel button: pressed while the mouse is
// down, released on mouse up or if the mouse leaves while dragging - matches
// how a real front-panel button behaves (no click-to-toggle).
//
// Also supports latching via double-click: some real TX81Z button combos
// need two keys held down *at once* (e.g. entering a diagnostic/alternate
// mode), which a single mouse pointer can't do with plain momentary clicks.
// Double-clicking a button latches it held (visually sunk + an amber
// outline); a second double-click releases it. This only changes how mouse
// gestures map to the same state.store(true/false) calls a normal click
// already uses - the emulated hardware can't tell a latch from a hand
// actually holding the button down.
class MomentaryButton : public juce::Component
{
public:
	MomentaryButton(juce::String label, std::atomic<bool> &target)
		: text(std::move(label)), state(target)
	{
	}

	// true exactly while the mouse is held down on this button (or it's
	// latched) - MODE SELECT's LEDs mirror this directly (lit while held,
	// off on release).
	bool isPressed() const { return state.load(); }

	void paint(juce::Graphics &g) override
	{
		auto bounds = getLocalBounds().toFloat();
		bool down = state.load();
		g.setColour(down ? juce::Colour(0xff585858) : juce::Colour(0xff3a3a3a));
		g.fillRoundedRectangle(bounds, 3.0f);
		g.setColour(juce::Colours::black.withAlpha(0.6f));
		g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);
		g.setColour(juce::Colours::whitesmoke);
		g.setFont(juce::Font(juce::FontOptions(juce::jmax(8.0f, getHeight() * 0.22f))));
		g.drawFittedText(text, getLocalBounds().reduced(2), juce::Justification::centred, 3);

		if (latched)
		{
			g.setColour(juce::Colours::orange);
			g.drawRoundedRectangle(bounds.reduced(1.5f), 3.0f, 2.0f);
		}
	}

	void mouseDown(const juce::MouseEvent &) override { if (!latched) { state.store(true); repaint(); } }
	void mouseUp(const juce::MouseEvent &) override { if (!latched) { state.store(false); repaint(); } }
	void mouseExit(const juce::MouseEvent &e) override
	{
		if (latched || !e.mods.isAnyMouseButtonDown())
			return;
		state.store(false);
		repaint();
	}
	void mouseDoubleClick(const juce::MouseEvent &) override
	{
		latched = !latched;
		state.store(latched);
		repaint();
	}

private:
	juce::String text;
	std::atomic<bool> &state;
	bool latched = false;
};

// The POWER switch: a real latching push-button, not momentary - each click
// flips it and it stays put, same as the real hardware's power switch.
// Calls the processor's togglePower() (rather than writing the atomic
// directly) so it can also arm the real-time boot sequence.
class PowerButton : public juce::Component
{
public:
	PowerButton(std::atomic<bool> &target, Tx81zAudioProcessor &proc) : state(target), processor(proc) {}

	void paint(juce::Graphics &g) override
	{
		auto bounds = getLocalBounds().toFloat();
		bool on = state.load();
		g.setColour(on ? juce::Colour(0xff4a4a4a) : juce::Colour(0xff2a2a2a));
		g.fillRoundedRectangle(bounds, 3.0f);
		g.setColour(juce::Colours::black.withAlpha(0.7f));
		g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);
		g.setColour(on ? juce::Colour(0xff40e050) : juce::Colour(0xff505050));
		g.fillEllipse(bounds.getCentreX() - 4.0f, bounds.getY() + 6.0f, 8.0f, 8.0f);
	}

	void mouseDown(const juce::MouseEvent &) override
	{
		processor.togglePower();
		repaint();
	}

private:
	std::atomic<bool> &state;
	Tx81zAudioProcessor &processor;
};

// A small triangular left/right button (PARAMETER, MASTER VOLUME). Supports
// the same double-click latch mechanism as MomentaryButton - see its comment.
class TriangleButton : public juce::Component
{
public:
	TriangleButton(bool pointsRight, std::atomic<bool> &target)
		: right(pointsRight), state(target)
	{
	}

	void paint(juce::Graphics &g) override
	{
		auto bounds = getLocalBounds().toFloat();
		bool down = state.load();
		g.setColour(down ? juce::Colour(0xff585858) : juce::Colour(0xff3a3a3a));
		g.fillRoundedRectangle(bounds, 3.0f);
		g.setColour(juce::Colours::black.withAlpha(0.6f));
		g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);

		juce::Path tri;
		auto b = bounds.reduced(bounds.getWidth() * 0.3f, bounds.getHeight() * 0.25f);
		if (right)
		{
			tri.addTriangle(b.getX(), b.getY(), b.getX(), b.getBottom(), b.getRight(), b.getCentreY());
		}
		else
		{
			tri.addTriangle(b.getRight(), b.getY(), b.getRight(), b.getBottom(), b.getX(), b.getCentreY());
		}
		g.setColour(juce::Colours::whitesmoke);
		g.fillPath(tri);

		if (latched)
		{
			g.setColour(juce::Colours::orange);
			g.drawRoundedRectangle(bounds.reduced(1.5f), 3.0f, 2.0f);
		}
	}

	void mouseDown(const juce::MouseEvent &) override { if (!latched) { state.store(true); repaint(); } }
	void mouseUp(const juce::MouseEvent &) override { if (!latched) { state.store(false); repaint(); } }
	void mouseExit(const juce::MouseEvent &e) override
	{
		if (latched || !e.mods.isAnyMouseButtonDown())
			return;
		state.store(false);
		repaint();
	}
	void mouseDoubleClick(const juce::MouseEvent &) override
	{
		latched = !latched;
		state.store(latched);
		repaint();
	}

private:
	bool right;
	std::atomic<bool> &state;
	bool latched = false;
};

// The actual front panel, always laid out at a fixed reference resolution
// (kRefWidth x kRefHeight). The outer editor scales this whole component via
// a transform to fit whatever window size the user drags to, so none of this
// class's drawing/layout code needs to know about resizing at all.
class Tx81zPanel : public juce::Component, private juce::Timer
{
public:
	static constexpr int kRefWidth = 1250;
	static constexpr int kRefHeight = 170;

	explicit Tx81zPanel(Tx81zAudioProcessor &);

	void paint(juce::Graphics &) override;
	void resized() override;

private:
	void timerCallback() override;

	Tx81zAudioProcessor &processor;

	PowerButton powerBtn;
	MomentaryButton storeEqCopyBtn, utilityBtn, editCompareBtn, playPerformBtn;
	MomentaryButton dataEntryDecBtn, dataEntryIncBtn;
	MomentaryButton cursorBtn;
	TriangleButton paramLeftBtn, paramRightBtn;
	TriangleButton volDownBtn, volUpBtn;

	juce::Rectangle<int> lcdArea;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Tx81zPanel)
};

class Tx81zAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
	explicit Tx81zAudioProcessorEditor(Tx81zAudioProcessor &);

	void resized() override;

private:
	Tx81zPanel panel;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Tx81zAudioProcessorEditor)
};
