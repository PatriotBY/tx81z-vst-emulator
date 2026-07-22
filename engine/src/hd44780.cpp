// Standalone port of MAME's HD44780 LCD controller - see header comment for scope.
#include "tx81z/hd44780.h"

namespace tx81z {

void Hd44780::reset()
{
	m_ddram.fill(0x20); // filled with SPACE
	m_cgram.fill(0);

	m_ac = 0;
	m_dr = 0;
	m_ir = 0;
	m_active_ram = DDRAM;
	m_display_on = false;
	m_cursor_on = false;
	m_blink_on = false;
	m_shift_on = false;
	m_direction = 1;
	m_num_line = 1;
	m_char_size = 8;
	m_disp_shift = 0;
}

void Hd44780::correct_ac()
{
	if (m_active_ram == DDRAM)
	{
		int max_ac = (m_num_line == 1) ? 0x4f : 0x67;

		if (m_ac > max_ac)
			m_ac -= max_ac + 1;
		else if (m_ac < 0)
			m_ac = max_ac;
		else if (m_num_line == 2 && m_ac > 0x27 && m_ac < 0x40)
			m_ac = 0x40 + (m_ac - 0x28);
	}
	else
	{
		m_ac &= 0x3f;
	}
}

void Hd44780::update_ac(int direction)
{
	if (m_active_ram == DDRAM && m_num_line == 2 && direction == -1 && m_ac == 0x40)
		m_ac = 0x27;
	else
		m_ac += direction;

	correct_ac();
}

void Hd44780::shift_display(int direction)
{
	m_disp_shift += direction;

	if (m_disp_shift == 0x50)
		m_disp_shift = 0;
	else if (m_disp_shift == -1)
		m_disp_shift = 0x4f;
}

u8 Hd44780::read(u8 offset)
{
	switch (offset & 0x01)
	{
	case 0: return control_read();
	case 1: return data_read();
	}
	return 0;
}

void Hd44780::write(u8 offset, u8 data)
{
	switch (offset & 0x01)
	{
	case 0: control_write(data); break;
	case 1: data_write(data); break;
	}
}

void Hd44780::control_write(u8 data)
{
	m_ir = data;

	if (m_ir & 0x80)
	{
		// set DDRAM address
		m_active_ram = DDRAM;
		m_ac = m_ir & 0x7f;
		correct_ac();
		return;
	}
	else if (m_ir & 0x40)
	{
		// set CGRAM address
		m_active_ram = CGRAM;
		m_ac = m_ir & 0x3f;
		return;
	}
	else if (m_ir & 0x20)
	{
		// function set
		m_char_size = (m_ir & 0x08) ? 8 : ((m_ir & 0x04) ? 10 : 8); // 5x10 unavailable in 2-line mode
		m_num_line = ((m_ir & 0x08) ? 1 : 0) + 1;
		correct_ac();
		return;
	}
	else if (m_ir & 0x10)
	{
		// cursor or display shift
		int direction = (m_ir & 0x04) ? +1 : -1;
		if (m_ir & 0x08)
			shift_display(direction);
		else
			update_ac(direction);
	}
	else if (m_ir & 0x08)
	{
		// display on/off control
		m_display_on = (m_ir & 0x04) != 0;
		m_cursor_on = (m_ir & 0x02) != 0;
		m_blink_on = (m_ir & 0x01) != 0;
	}
	else if (m_ir & 0x04)
	{
		// entry mode set
		m_direction = (m_ir & 0x02) ? +1 : -1;
		m_shift_on = (m_ir & 0x01) != 0;
	}
	else if (m_ir & 0x02)
	{
		// return home
		m_ac = 0;
		m_active_ram = DDRAM;
		m_direction = 1;
		m_disp_shift = 0;
	}
	else if (m_ir & 0x01)
	{
		// clear display
		m_ac = 0;
		m_active_ram = DDRAM;
		m_direction = 1;
		m_disp_shift = 0;
		m_ddram.fill(0x20);
	}
}

u8 Hd44780::control_read()
{
	// busy flag (bit7) always reports clear - see header comment
	return m_ac & 0x7f;
}

void Hd44780::data_write(u8 data)
{
	m_dr = data;

	if (m_active_ram == DDRAM)
		m_ddram[m_ac] = m_dr;
	else
		m_cgram[m_ac & 0x3f] = m_dr;

	update_ac(m_direction);
	if (m_shift_on)
		shift_display(m_direction);
}

u8 Hd44780::data_read()
{
	u8 data = (m_active_ram == DDRAM) ? m_ddram[m_ac] : m_cgram[m_ac & 0x3f];
	update_ac(m_direction);
	return data;
}

std::string Hd44780::display_text(int num_lines, int chars_per_line) const
{
	std::string out;
	int line_size = 80 / (num_lines > 0 ? num_lines : 1);

	for (int line = 0; line < num_lines; ++line)
	{
		for (int pos = 0; pos < chars_per_line; ++pos)
		{
			int char_pos = line * 0x40 + ((pos + m_disp_shift) % line_size);
			u8 c = m_ddram[char_pos];
			out += (c >= 0x20 && c < 0x7f) ? char(c) : '.';
		}
		if (line + 1 < num_lines)
			out += '\n';
	}
	return out;
}

} // namespace tx81z
