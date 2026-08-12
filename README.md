NextUI netplay builds
--------------------------------------------------------------------------------

The network-enabled libretro build includes a hardened Game Link transport with
complete packet reads and writes, `TCP_NODELAY`, bounded socket operations, and
connection pacing diagnostics. Build it with `HAVE_NETWORK=1`.

An experimental dual-instance build links two Gambatte instances through an
in-process serial coordinator. It is enabled separately so it does not alter the
ordinary core:

    make -f Makefile.libretro HAVE_NETWORK=1 NETPLAY_DUAL_INSTANCE=1

This build identifies itself as `Gambatte Dual v0.5.0-netdual7`. It provides two
input ports and a frame-boundary service scheduler for timing-sensitive link
games. Frontends may load separate cartridges with the `gblc` libretro
subsystem, select console A or B for presentation, access each console's SRAM
and RTC independently, and create paired checkpoints at idle serial boundaries.
The versioned extension ABI is declared in
`libgambatte/libretro/gambatte_dual.h`. Ordinary libretro save states remain
disabled so a frontend cannot accidentally save only one linked console.


About
--------------------------------------------------------------------------------
Gambatte is an accuracy-focused, open-source, cross-platform
Game Boy Color emulator written in C++. It is based on hundreds of
corner case hardware tests, as well as previous documentation and reverse
engineering efforts.

The core emulation code is contained in a separate library back-end
(libgambatte) written in platform-independent C++. There is currently a GUI
front-end (gambatte_qt) using Trolltech's Qt4 toolkit, and a simple command-line
SDL front-end (gambatte_sdl).

The GUI front-end contains platform-specific extensions for video, sound and
timers. It should work on MS Windows, Linux/BSD/UNIX-like OSes, and Mac OS X.

The SDL front-end should be usable on all platforms with a working SDL port. It
should also be quite trivial to create new (simple) front-ends (note that the
library API should in no way be considered stable).

Usage
--------------------------------------------------------------------------------
You will have to supply Gambatte with a ROM image file of the GB/GBC
program/game you'd like to run/play, either as a command-line argument to
gambatte_sdl, or through the File->Open... menu in gambatte_qt.

gambatte_sdl keyboard commands:
TAB    - fast-forward
Ctrl-f - toggle full screen
Ctrl-r - reset
F5     - save state
F6     - previous state slot
F7     - next state slot
F8     - load state
0 to 9 - select state slot 0 to 9

Default key mapping:
Up:     Up
Down:   Down
Left:   Left
Right:  Right
A:      D
B:      C
Start:  Return
Select: Shift

Compiling
--------------------------------------------------------------------------------
Building Gambatte from source code can be done by executing the
build_<qt/sdl>.sh scripts for the qt/sdl front-ends respectively, or by issueing
the correct build command (either 'scons' or 'qmake && make') in the top-level
subdirectories (libgambatte will have to be built first). The clean.sh script
can be executed to remove all generated files after a compile (including
binaries).

Requirements for building libgambatte:
- A decent C++ compiler (like g++ in the GNU Compiler Collection).
- SCons.
- optionally zlib

Requirements for building gambatte_sdl:
- A decent C++ compiler (like g++ in the GNU Compiler Collection).
- SDL headers and library.
- SCons.
(- libgambatte.)

Requirements for building gambatte_qt:
- A decent C++ compiler (like g++ in the GNU Compiler Collection).
- Qt4 (Core, GUI, OpenGL) headers and library.
- Qmake and make (GNU Make should work).
- Platform-specific requirements:
  * MS Windows:
    - Win32 API headers and libraries.
    - DirectSound and DirectDraw7 headers and libraries.
    - Direct3D9 headers
  * Linux/BSD/UNIX-like OSes:
    - POSIX/UNIX headers (unistd.h, fcntl.h, sys/ioctl.h, sys/time.h, sys/shm.h).
    - Open Sound System header (sys/soundcard.h).
    - X11 Xlib, XVideo, XRandR and XShm headers and libraries.
    - Alsa headers and library (Linux only).
  * Max OS X:
    - Recent Mac OS X SDK (Panther Xcode/SDK should work)
(- libgambatte.)

Installing after a compile simply amounts to copying the generated binary
(either gambatte_qt/bin/gambatte_qt<.exe> or gambatte_sdl/gambatte_sdl<.exe>)
to wherever you'd like to keep it.

--------------------------------------------------------------------------------


Copyright (C) 2007 by Sindre Aamås
aamas@stud.ntnu.no

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License version 2 for more details.

You should have received a copy of the GNU General Public License
version 2 along with this program; if not, write to the
Free Software Foundation, Inc.,
59 Temple Place - Suite 330, Boston, MA  02111-1307, USA





Thanks
--------------------------------------------------------------------------------
Derek Liauw Kie Fa (Kreed)
Gilles Vollant
Jeff Frohwein
Jonathan Gevaryahu (Lord Nightmare)
kOOPa
Marat Fayzullin
Martin Korth (nocash)
Maxim Stepin (MaxSt)
Nach
Pan of Anthrox
Pascal Felber
Paul Robson
SDL
Shay Green (blargg)
The OpenGL Extension Wrangler Library

--------------------------------------------------------------------------------
Game Boy and Game Boy Color are registered trademarks of
Nintendo of America Inc.
Gambatte is not affiliated with or endorsed by any of the companies mentioned.
