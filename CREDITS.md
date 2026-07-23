# Credits

This project is based in part on the work of the MAME Development Team and
contributors, as well as other open-source projects. We sincerely thank all
developers whose work made this project possible.

In particular:

- **Aaron Giles** - for the original MAME HD6303X/6800-family CPU core and
  opcode tables that this project's engine ports, and for the standalone
  [ymfm](https://github.com/aaronsgiles/ymfm) FM synthesis library that
  powers this project's YM2414/OPZ sound chip emulation.
- **Sandro Ronco** - for the original MAME HD44780 LCD controller
  implementation this project's engine ports.
- **AJR** - for the MAME Yamaha TX81Z driver (`ymtx81z.cpp`) that documents
  the real hardware's memory map and peripheral wiring this project is
  based on.
- **The MAME Development Team and contributors** at large, for building and
  maintaining the most thoroughly documented hardware-emulation project in
  existence, without which reverse-engineering this hardware from scratch
  would not have been feasible.
- **The JUCE / Raw Material Software team**, for the JUCE framework this
  plugin is built on.

See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) for full license
details of each component.
