#include "PluginEditor.h"

namespace {
const juce::Colour kPanelBg(0xff1c1c1e);
const juce::Colour kLabelGrey(0xffb8b8b8);
const juce::Colour kLcdBg(0xff10150d);
const juce::Colour kLcdText(0xffb8e060);
const juce::Colour kLedOff(0xff3a3a3a);
const juce::Colour kLedOn(0xffe03020);
}

Tx81zPanel::Tx81zPanel(Tx81zAudioProcessor &p)
	: processor(p)
	, powerBtn(p.powerOn, p)
	, storeEqCopyBtn("STORE/\nEQ COPY", p.buttons.storeEqCopy)
	, utilityBtn("UTILITY", p.buttons.utility)
	, editCompareBtn("EDIT/\nCOMPARE", p.buttons.editCompare)
	, playPerformBtn("PLAY/\nPERFORM", p.buttons.playPerform)
	, dataEntryDecBtn("DEC", p.buttons.dataEntryDec)
	, dataEntryIncBtn("INC", p.buttons.dataEntryInc)
	, cursorBtn("CURSOR", p.buttons.cursor)
	, paramLeftBtn(false, p.buttons.paramLeft)
	, paramRightBtn(true, p.buttons.paramRight)
	, volDownBtn(false, p.buttons.masterVolDown)
	, volUpBtn(true, p.buttons.masterVolUp)
{
	juce::Component *comps[] = { &powerBtn, &storeEqCopyBtn, &utilityBtn, &editCompareBtn, &playPerformBtn,
	                             &dataEntryDecBtn, &dataEntryIncBtn, &cursorBtn,
	                             &paramLeftBtn, &paramRightBtn, &volDownBtn, &volUpBtn };
	for (auto *c : comps)
		addAndMakeVisible(c);

	setSize(kRefWidth, kRefHeight);
	startTimerHz(25);
}

void Tx81zPanel::timerCallback()
{
	// Repaint the whole panel, not just the LCD area: a partial repaint()
	// call clips the Graphics context to that region, so anything drawn
	// outside it (like the MODE SELECT LEDs) would silently never actually
	// reach the screen even though paint() still runs - this was why the
	// LEDs never visibly lit up on press. The panel is small enough that a
	// full repaint 25x/second is cheap.
	repaint();
}

void Tx81zPanel::paint(juce::Graphics &g)
{
	auto bounds = getLocalBounds();
	g.fillAll(kPanelBg);

	// screw holes
	int holeR = 7;
	for (auto pt : { juce::Point<int>(20, 18), juce::Point<int>(20, bounds.getHeight() - 18),
	                 juce::Point<int>(bounds.getWidth() - 20, 18), juce::Point<int>(bounds.getWidth() - 20, bounds.getHeight() - 18) })
	{
		g.setColour(juce::Colours::whitesmoke.withAlpha(0.85f));
		g.fillEllipse(float(pt.x - holeR), float(pt.y - holeR), float(holeR * 2), float(holeR * 2));
	}

	auto groupLabel = [&](juce::Rectangle<int> area, const juce::String &text)
	{
		g.setColour(kLabelGrey);
		g.setFont(juce::Font(juce::FontOptions(11.0f)).boldened());
		g.drawFittedText(text, area, juce::Justification::centred, 1);
	};

	// POWER
	groupLabel({ 15, 22, 80, 16 }, "POWER");
	g.setColour(kLabelGrey);
	g.setFont(juce::Font(juce::FontOptions(9.0f)));
	g.drawFittedText("ON/OFF", powerBtn.getBounds().translated(0, powerBtn.getHeight() + 6), juce::Justification::centred, 1);

	// LCD bezel
	g.setColour(kLcdBg);
	g.fillRoundedRectangle(lcdArea.toFloat(), 3.0f);
	g.setColour(juce::Colours::black);
	g.drawRoundedRectangle(lcdArea.toFloat().reduced(0.5f), 3.0f, 1.5f);

	// operate on a local copy - lcdArea is a member and removeFromLeft()/
	// removeFromTop() mutate their receiver, so using the member directly
	// here would shrink it a little more on every single repaint (this was
	// a real bug: it's what caused the LCD to visibly nest/shrink over time)
	auto lcdWorkArea = lcdArea;
	auto brandArea = lcdWorkArea.removeFromLeft(lcdWorkArea.getWidth() * 2 / 5).reduced(8, 6);
	g.setColour(juce::Colours::white);
	g.setFont(juce::Font(juce::FontOptions(15.0f)).boldened());
	g.drawText("YAMAHA", brandArea.removeFromTop(brandArea.getHeight() / 3), juce::Justification::centredLeft);
	g.setColour(kLabelGrey);
	g.setFont(juce::Font(juce::FontOptions(9.0f)));
	g.drawText("FM TONE GENERATOR", brandArea.removeFromTop(brandArea.getHeight() / 2), juce::Justification::centredLeft);
	g.setColour(juce::Colour(0xffd05030));
	g.setFont(juce::Font(juce::FontOptions(13.0f)).boldened());
	g.drawText("TX81Z", brandArea, juce::Justification::centredLeft);

	juce::String line1, line2;
	processor.getLcdText(line1, line2);
	auto textArea = lcdWorkArea.reduced(10, 4);
	g.setColour(kLcdText);
	g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 16.0f, juce::Font::plain)));
	g.drawText(line1, textArea.removeFromTop(textArea.getHeight() / 2), juce::Justification::centredLeft);
	g.drawText(line2, textArea, juce::Justification::centredLeft);

	// Group labels sit up near the LCD's top edge; LEDs sit in the middle
	// gap between the labels and the button row (which now sits low, near
	// the LCD's bottom edge, per the reference photo).
	const int groupLabelY = 22, groupLabelH = 16;
	const int ledY = 54;

	// MODE SELECT group label + LEDs
	auto modeLeds = juce::Rectangle<int>(storeEqCopyBtn.getX(), groupLabelY,
		playPerformBtn.getRight() - storeEqCopyBtn.getX(), groupLabelH);
	groupLabel(modeLeds, "MODE SELECT");

	// LEDs mirror each button's own pressed state directly (lit while held,
	// off on release) - see the Yamaha TX81Z owner's manual for how the real
	// LEDs behave (mode indicators that also blink for note-in/compare
	// state), which is more nuanced than we model yet; this is a placeholder
	// approximation until the real port6 LED-bit-to-button mapping is solved.
	MomentaryButton *modeButtons[4] = { &storeEqCopyBtn, &utilityBtn, &editCompareBtn, &playPerformBtn };
	for (int i = 0; i < 4; ++i)
	{
		bool on = modeButtons[i]->isPressed();
		g.setColour(on ? kLedOn : kLedOff);
		int cx = modeButtons[i]->getBounds().getCentreX();
		g.fillEllipse(float(cx - 4), float(ledY - 4), 8.0f, 8.0f);
	}

	// PARAMETER label
	groupLabel({ paramLeftBtn.getX(), groupLabelY, paramRightBtn.getRight() - paramLeftBtn.getX(), groupLabelH }, "PARAMETER");

	// DATA ENTRY label + sub-labels
	groupLabel({ dataEntryDecBtn.getX(), groupLabelY, dataEntryIncBtn.getRight() - dataEntryDecBtn.getX(), groupLabelH }, "DATA ENTRY");
	g.setFont(juce::Font(juce::FontOptions(8.5f)));
	g.setColour(kLabelGrey);
	g.drawFittedText("-1\nNO/OFF", dataEntryDecBtn.getBounds().translated(0, dataEntryDecBtn.getHeight() + 4), juce::Justification::centred, 2);
	g.drawFittedText("+1\nYES/ON", dataEntryIncBtn.getBounds().translated(0, dataEntryIncBtn.getHeight() + 4), juce::Justification::centred, 2);

	// MASTER VOLUME label + CURSOR bracket
	auto mvArea = juce::Rectangle<int>(volDownBtn.getX(), groupLabelY, cursorBtn.getRight() - volDownBtn.getX(), groupLabelH);
	groupLabel(mvArea, "MASTER VOLUME");

	int bracketY = volDownBtn.getBottom() + 14;
	g.setColour(kLabelGrey);
	g.drawLine(float(volDownBtn.getX()), float(bracketY), float(cursorBtn.getRight()), float(bracketY), 1.0f);
	g.drawLine(float(volDownBtn.getX()), float(bracketY), float(volDownBtn.getX()), float(bracketY - 5), 1.0f);
	g.drawLine(float(cursorBtn.getRight()), float(bracketY), float(cursorBtn.getRight()), float(bracketY - 5), 1.0f);
	g.setFont(juce::Font(juce::FontOptions(9.0f)));
	g.drawFittedText("CURSOR", { volDownBtn.getX(), bracketY + 2, cursorBtn.getRight() - volDownBtn.getX(), 14 }, juce::Justification::centred, 1);

	// PHONES jack - positioned relative to the last control group, not a
	// fixed offset from the panel width (that was the bug that made it
	// overlap Data Entry/Master Volume once those groups' real total width
	// didn't match the old, too-narrow reference canvas)
	int phonesX = cursorBtn.getRight() + 50, phonesY = 60;
	groupLabel({ phonesX - 40, 22, 80, 16 }, "PHONES");
	g.setColour(juce::Colour(0xff0a0a0a));
	g.fillEllipse(float(phonesX - 16), float(phonesY - 16), 32.0f, 32.0f);
	g.setColour(kLabelGrey);
	g.drawEllipse(float(phonesX - 16), float(phonesY - 16), 32.0f, 32.0f, 1.5f);

	if (!processor.isReady())
	{
		g.setColour(juce::Colours::orange);
		g.setFont(juce::Font(juce::FontOptions(11.0f)));
		g.drawFittedText("Not ready: " + processor.resourceStatus(), bounds.removeFromBottom(16), juce::Justification::centred, 1);
	}
}

void Tx81zPanel::resized()
{
	lcdArea = { 105, 20, 330, 90 };

	powerBtn.setBounds(25, 45, 50, 45);

	// Button row's *bottom* edge lines up with the LCD's bottom edge (per
	// the reference photo), not below it - leaves room above for the group
	// labels + LED row (see paint()).
	int h = 40;
	int y = lcdArea.getBottom() - h;
	int x = 470;

	auto place = [&](juce::Component &c, int w)
	{
		c.setBounds(x, y, w, h);
		x += w + 8;
	};

	place(storeEqCopyBtn, 58);
	place(utilityBtn, 58);
	place(editCompareBtn, 58);
	place(playPerformBtn, 58);

	x += 24;
	place(paramLeftBtn, 34);
	place(paramRightBtn, 34);

	x += 24;
	place(dataEntryDecBtn, 44);
	place(dataEntryIncBtn, 44);

	x += 24;
	place(volDownBtn, 34);
	place(volUpBtn, 34);
	x += 12;
	place(cursorBtn, 44);
}

Tx81zAudioProcessorEditor::Tx81zAudioProcessorEditor(Tx81zAudioProcessor &p)
	: AudioProcessorEditor(&p), panel(p)
{
	addAndMakeVisible(panel);

	setResizable(true, true);
	// Panel is always laid out/painted at its fixed reference resolution and
	// scaled up/down via a transform (see resized() below), so any size the
	// user drags to just works - nothing about the drawing code needs to
	// know the window resized at all.
	getConstrainer()->setFixedAspectRatio(double(Tx81zPanel::kRefWidth) / double(Tx81zPanel::kRefHeight));
	setResizeLimits(500, 85, 2000, 340);

	setSize(Tx81zPanel::kRefWidth, Tx81zPanel::kRefHeight);
}

void Tx81zAudioProcessorEditor::resized()
{
	float scale = getWidth() / float(Tx81zPanel::kRefWidth);
	panel.setTransform(juce::AffineTransform::scale(scale));
}
