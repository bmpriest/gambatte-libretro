//
//   Copyright (C) 2007 by sinamas <sinamas at users.sourceforge.net>
//
//   This program is free software; you can redistribute it and/or modify
//   it under the terms of the GNU General Public License version 2 as
//   published by the Free Software Foundation.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//   GNU General Public License version 2 for more details.
//
//   You should have received a copy of the GNU General Public License
//   version 2 along with this program; if not, write to the
//   Free Software Foundation, Inc.,
//   51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
//

#ifndef HuC3Chip_H
#define HuC3Chip_H

enum
{
    HUC3_READ = 0,
    HUC3_WRITE = 1,
    HUC3_NONE = 2
};

#include "../../include/timesource.h"

#include <ctime>
#include <stdint.h>

namespace gambatte {

struct SaveState;

class HuC3Chip {
public:
	HuC3Chip();

	/* Null - the default, and every ordinary session - means read the host
	 * clock. See timesource.h for why a paired session cannot. */
	void setTimeSource(TimeSource *timeSource) { timeSource_ = timeSource; }

	uint64_t baseTime() const { return baseTime_; }
	void setBaseTime(uint64_t baseTime) { baseTime_ = baseTime; }

	/* Move this clock's origin with its time source, so that however long the
	 * cartridge thinks it has been running stays what it was. */
	void shiftBase(int64_t seconds) {
		baseTime_ = (uint64_t)((int64_t)baseTime_ + seconds);
		haltTime_ = (uint64_t)((int64_t)haltTime_ + seconds);
	}

	uint64_t& getBaseTime()
	{
		return baseTime_;
	}

	void saveState(SaveState &state) const;
	void loadState(SaveState const &state);
    void setRamflag(unsigned char ramflag) { ramflag_ = ramflag; irReceivingPulse_ = false;  }
    bool isHuC3() const { return enabled_; }

	void set(bool enabled) {
		enabled_ = enabled;
	}
    
    unsigned char read(unsigned p, unsigned long const cc);
	void write(unsigned p, unsigned data);

private:
	/* Time of day for this cartridge. Every reading in huc3.cpp goes through
	 * here so that a paired session has exactly one place where the clock is
	 * decided. */
	uint64_t now() const {
		return timeSource_ ? timeSource_->now()
		                   : static_cast<uint64_t>(std::time(0));
	}

	TimeSource *timeSource_;
	uint64_t baseTime_;
	uint64_t haltTime_;
	unsigned dataTime_;
    unsigned writingTime_;
    unsigned char ramValue_;
    unsigned char shift_;
    unsigned char ramflag_;
    unsigned char modeflag_;
    unsigned long irBaseCycle_;
	bool enabled_;
    bool halted_;
    bool irReceivingPulse_;

	void doLatch();
    void updateTime();
};

}

#endif
