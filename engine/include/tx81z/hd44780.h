// Standalone port of MAME's HD44780 LCD controller
// (src/devices/video/hd44780.cpp, license:BSD-3-Clause, copyright-holders:Sandro Ronco),
// scoped to how the TX81Z actually drives it: direct 8-bit bus access (offset 0 =
// control/instruction register, offset 1 = data register), m_data_len always 8 so
// the 4-bit nibble-interface mode is not implemented. The busy-flag timing delay
// (attotime-based in MAME) is simplified to "never busy" - commands complete
// instantly - since we have no equivalent real-time scheduler here; this only
// matters if firmware polls the busy flag rather than using fixed delays, and
// either way makes boot strictly less likely to hang, not more.
#pragma once

#include "tx81z/types.h"

#include <array>
#include <string>

namespace tx81z {

class Hd44780
{
public:
	Hd44780() { reset(); }

	void reset();

	u8 read(u8 offset);       // offset 0 = control (busy flag + address counter), 1 = data
	void write(u8 offset, u8 data);

	// Debug/GUI helper: renders the visible character grid as text (space for
	// blank cells), independent of MAME's pixel-based render() - useful for
	// confirming firmware boot progress before a real front-panel GUI exists.
	// TX81Z wires this as a 2x16 display (see ymtx81z.cpp's HD44780 config).
	std::string display_text(int num_lines, int chars_per_line) const;

private:
	enum ActiveRam { DDRAM, CGRAM };

	void correct_ac();
	void update_ac(int direction);
	void shift_display(int direction);

	u8 control_read();
	void control_write(u8 data);
	u8 data_read();
	void data_write(u8 data);

	std::array<u8, 80> m_ddram{};
	std::array<u8, 64> m_cgram{};

	u8 m_ir = 0, m_dr = 0;
	int m_ac = 0;
	ActiveRam m_active_ram = DDRAM;

	bool m_display_on = false, m_cursor_on = false, m_blink_on = false, m_shift_on = false;
	int m_direction = 1;
	int m_num_line = 1;
	int m_char_size = 8;
	int m_disp_shift = 0;
};

} // namespace tx81z
