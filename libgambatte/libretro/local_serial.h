#ifndef NETPLAY_GAMBATTE_LOCAL_SERIAL_H
#define NETPLAY_GAMBATTE_LOCAL_SERIAL_H

#include "serial_io.h"
#include <pthread.h>
#include <stdint.h>

struct NetplayLocalSerialStats {
	uint64_t exchanges;
	uint64_t send_timeouts;
	uint64_t poll_timeouts;
	uint64_t simultaneous_sends;
	uint64_t total_wait_us;
	uint32_t longest_wait_us;
};

/* In-process two-console cable for the paired wrapper.
 *
 * Every answer this bus gives becomes emulated state - the byte a console
 * receives, and whether it receives one at all - so two devices mirroring the
 * same pair must get identical answers. That means no decision here may depend
 * on wall-clock time or on which thread happens to be running.
 *
 * The previous implementation decided both by thread position: check() was a
 * true poll answering from the peer's live state, send() gave up after 50ms of
 * real time and fabricated 0xFF, and simultaneous clock attempts were resolved
 * by whoever won the mutex. On two handhelds running the same game from the
 * same checkpoint that produced 385 exchanges on one device and 0 on the other
 * over the same window.
 *
 * Here every decision is a comparison of *emulated* cycles:
 *
 *   advance(ep, cc)  publishes how far a console has emulated. Called from the
 *                    CPU loop, so a console that is merely busy still reports
 *                    progress and never strands its peer.
 *   check(ep, cc)    answers whether a request exists at a cycle this console
 *                    has already reached. If the peer has not yet emulated as
 *                    far as this console, it waits - because the peer may still
 *                    post a request in the interval, and answering "nothing"
 *                    early is exactly how the two devices came to disagree.
 *   send(ep, cc)     posts a request and waits for the peer to consume it, or
 *                    until the peer has emulated past a fixed cycle deadline.
 *                    A timeout is still possible - a game can leave a link
 *                    half-open - but it now happens at the same emulated cycle
 *                    on both devices.
 *
 * Deadlock is avoided by never waiting on a peer that is already ahead, is
 * inactive, or has a request outstanding: a console that is behind always makes
 * progress, and a console that is blocked in send() has a request pending which
 * its peer can consume immediately.
 */
class NetplayLocalSerialBus {
public:
	NetplayLocalSerialBus();
	~NetplayLocalSerialBus();

	void reset();

	/* Called by the wrapper once per paired frame. Both consoles' cycle
	 * counters are private to them and are rebased by savestates, so the bus
	 * measures each one relative to where it started this frame. */
	void beginFrame();

	/* Two different states, and conflating them deadlocks the pair. "Running"
	 * is a console still emulating its own frame; "active" is one still willing
	 * to answer its peer. A console that has finished stops running but stays
	 * active, so it can service a late transfer - and its peer can tell that
	 * nothing more is coming and stop waiting. */
	void setRunning(unsigned endpoint, bool running);
	void setActive(unsigned endpoint, bool active);
	bool peerActive(unsigned endpoint);
	bool isIdle();

	/* Emulated progress report. Never blocks. */
	void advance(unsigned endpoint, unsigned long cc);

	/* For a console that has finished its own frame while its peer is still
	 * running. Blocks until the peer either has a request to answer or has
	 * finished too, and returns whether there is something to service.
	 *
	 * Replaces a 250us timed poll. That poll was both a correctness problem -
	 * how many times it fired depended on the scheduler - and most of the frame
	 * budget: it ran about sixty times per frame, which is roughly 15ms of the
	 * 16.67ms available, while the emulation underneath took two. */
	bool waitForService(unsigned endpoint);

	unsigned char send(unsigned endpoint, unsigned long cc, unsigned char data, bool fastCgb);
	bool check(unsigned endpoint, unsigned long cc, unsigned char out,
	           unsigned char& in, bool& fastCgb);

	void snapshot(NetplayLocalSerialStats& stats);

private:
	NetplayLocalSerialBus(const NetplayLocalSerialBus&);
	NetplayLocalSerialBus& operator=(const NetplayLocalSerialBus&);

	/* Caller holds mutex_. Relative cycle position of an endpoint this frame. */
	uint64_t position(unsigned endpoint) const;
	void publish(unsigned endpoint, unsigned long cc);
	void waitReporting(const char* who, unsigned endpoint);

	pthread_mutex_t mutex_;
	pthread_cond_t changed_;
	bool stopped_;
	bool reported_stall_;

	/* One outstanding request per endpoint, each stamped with the emulated
	 * position it was made at. A single shared slot could only be arbitrated by
	 * who reached it first, which is the thread scheduler deciding emulated
	 * state. With a slot each, both consoles clocking at the same moment is a
	 * symmetric exchange needing no arbitration at all, and a request is only
	 * ever consumed by a peer that has emulated past the cycle it was made at. */
	bool req_active_[2];
	unsigned char req_byte_[2];
	bool req_fast_[2];
	uint64_t req_at_[2];
	bool resp_ready_[2];
	unsigned char resp_byte_[2];

	bool active_[2];
	bool running_[2];
	bool based_[2];
	unsigned long base_[2];
	uint64_t pos_[2];

	uint64_t generation_;
	uint64_t exchanges_;
	uint64_t send_timeouts_;
	uint64_t poll_timeouts_;
	uint64_t simultaneous_sends_;
	uint64_t total_wait_us_;
	uint32_t longest_wait_us_;
};

class NetplayLocalSerialEndpoint : public gambatte::SerialIO {
public:
	NetplayLocalSerialEndpoint(NetplayLocalSerialBus& bus, unsigned endpoint)
		: bus_(bus), endpoint_(endpoint) {}

	virtual void advance(unsigned long cc) { bus_.advance(endpoint_, cc); }

	virtual bool check(unsigned long cc, unsigned char out, unsigned char& in,
	                   bool& fastCgb) {
		return bus_.check(endpoint_, cc, out, in, fastCgb);
	}

	virtual unsigned char send(unsigned long cc, unsigned char data, bool fastCgb) {
		return bus_.send(endpoint_, cc, data, fastCgb);
	}

private:
	NetplayLocalSerialBus& bus_;
	unsigned endpoint_;
};

#endif
