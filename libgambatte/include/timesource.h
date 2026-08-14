/***************************************************************************
 *   Copyright (C) 2026                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License version 2 as     *
 *   published by the Free Software Foundation.                            *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License version 2 for more details.                *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   version 2 along with this program; if not, write to the               *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#ifndef TIMESOURCE_H
#define TIMESOURCE_H

#include <stdint.h>

namespace gambatte {

/* Power-on value for a paired cartridge's clock: 2020-01-01T00:00:00Z.
 *
 * setInitState seeds every cartridge clock from this in a NETPLAY_DUAL_INSTANCE
 * build, because the host clock it would otherwise read is exactly what two
 * mirrored devices cannot agree on. A frontend that installs real epochs later
 * moves each clock on from here, so both ends need the same starting value -
 * hence one definition rather than two constants that can drift apart.
 *
 * Arbitrary, but a plausible-looking date beats 0 for any game that shows one
 * before its save has supplied a real value. */
const uint64_t DUAL_POWER_ON_EPOCH = UINT64_C(1577836800);

/* Where a cartridge's real-time clock reads the time of day from.
 *
 * MBC3 and HuC3 carts consult the host clock directly (see mem/rtc.cpp and
 * mem/huc3.cpp). For a single emulated console that is exactly right. For two
 * consoles mirrored across two devices it cannot be: the answer becomes
 * emulated state the moment a game latches it, and no reading of two separate
 * wall clocks agrees.
 *
 * A frontend that needs the clock to be a function of emulated progress rather
 * than of the machine it is running on installs one of these per console. Left
 * unset - which is every ordinary build and every ordinary session - the
 * cartridge reads std::time(0) as before.
 */
class TimeSource {
public:
	virtual ~TimeSource() {}

	/* Seconds since the Unix epoch, as this cartridge's clock sees them. */
	virtual uint64_t now() const = 0;
};

}

#endif
