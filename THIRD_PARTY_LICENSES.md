# Third-Party Licenses

This project incorporates code from several third-party open-source
projects. This file lists each of them, their license, and where their
code is used in this repository. Original copyright notices and license
headers in the source files themselves have been preserved and are not
duplicated in full here except where noted.

This project as a whole (the combined work of the code below plus this
project's own original code) is distributed under the **GNU Affero General
Public License v3.0** - see [LICENSE](LICENSE) - because it links the JUCE
framework under JUCE's AGPLv3 option (see below). The permissively-licensed
components below (BSD-3-Clause) are compatible with, and included in, that
AGPLv3-licensed whole.

---

## MAME (Multiple Arcade Machine Emulator) Project

- **License**: BSD 3-Clause
- **Website / repository**: https://www.mamedev.org/ /
  https://github.com/mamedev/mame
- **Used for**: this project's engine (`engine/`) is a standalone,
  from-scratch port of the real Yamaha TX81Z hardware, using MAME's own
  driver and device source as the behavioral reference:
  - `engine/src/6800ops.hxx` is copied **verbatim** from MAME's
    `src/devices/cpu/m6800/6800ops.hxx`.
    Copyright-holder: **Aaron Giles**.
  - `engine/include/tx81z/cpu_core.h` and `engine/src/cpu_core.cpp` are a
    from-scratch port of the register model, opcode dispatch, and on-chip
    peripherals (timer, ports, SCI/UART) described in MAME's
    `src/devices/cpu/m6800/m6800.cpp` and `m6801.cpp`.
    Copyright-holder: **Aaron Giles**.
  - `engine/include/tx81z/hd44780.h` and `engine/src/hd44780.cpp` are a
    from-scratch port of MAME's `src/devices/video/hd44780.cpp` LCD
    controller.
    Copyright-holder: **Sandro Ronco**.
  - `engine/include/tx81z/tx81z_bus.h` and `engine/src/tx81z_bus.cpp`
    implement the memory map and peripheral wiring described in MAME's
    `src/mame/yamaha/ymtx81z.cpp` driver.
    Copyright-holder: **AJR**.

  The full BSD 3-Clause license text (as it appears verbatim in the
  `6800ops.hxx` file's originating project) is:

  ```
  BSD 3-Clause License

  Copyright (c) the respective copyright holder(s) named above
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this
     list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.

  3. Neither the name of the copyright holder nor the names of its
     contributors may be used to endorse or promote products derived from
     this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  ```

  Note: the full MAME source tree itself is **not** vendored into this
  repository (it was used only as a local, gitignored reference checkout
  during development) - only the files/logic explicitly listed above were
  ported or adapted into this project's own `engine/` code.

## ymfm

- **License**: BSD 3-Clause
- **Author**: Aaron Giles
- **Repository**: https://github.com/aaronsgiles/ymfm
- **Used for**: FM sound chip emulation (the real YM2414/OPZ chip). Vendored
  in full at `engine/third_party/ymfm/`, including its own
  [LICENSE](engine/third_party/ymfm/LICENSE) file, unmodified from upstream.

## JUCE

- **License**: this project uses the JUCE Framework under its **AGPLv3**
  open-source licensing option (as opposed to a paid commercial JUCE
  license).
- **Author / owner**: Raw Material Software Limited
- **Website**: https://juce.com
- **Used for**: the VST3 plugin framework (`plugin/`), fetched automatically
  at build time via CMake `FetchContent` - not vendored in this repository.

---

All trademarks, service marks, and trade names referenced above are the
property of their respective owners.
