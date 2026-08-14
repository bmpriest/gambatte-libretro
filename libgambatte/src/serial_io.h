#ifndef SERIAL_IO_H
#define SERIAL_IO_H

namespace gambatte {

class SerialIO
{
	public:
		virtual ~SerialIO() {};

		/* Emulated progress report, called from the CPU loop regardless of
		 * whether a transfer is in flight. A cable implementation that has to
		 * answer deterministically needs to know how far each console has got
		 * in emulated time; one that does not can ignore it. */
		virtual void advance(unsigned long /*cc*/) {}

		virtual bool check(unsigned long cc, unsigned char out, unsigned char& in, bool& fastCgb) = 0;
		virtual unsigned char send(unsigned long cc, unsigned char data, bool fastCgb) = 0;
};

}

#endif
