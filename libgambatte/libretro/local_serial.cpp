#include "local_serial.h"

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

namespace {

/* A cable that blocks on emulated conditions can, if one of those conditions is
 * wrong, block forever - and with two threads asleep on a condition variable
 * there is nothing left to interrogate. NETPLAY_BUS_STALL_MS makes each wait
 * report the whole bus state once if it has been waiting that long, which turns
 * a silent hang into a description of which predicate is stuck. Off unless the
 * variable is set. */
unsigned stall_report_ms() {
	static int cached = -1;
	if (cached < 0) {
		const char *v = getenv("NETPLAY_BUS_STALL_MS");
		cached = v && *v ? atoi(v) : 0;
		if (cached < 0) cached = 0;
	}
	return (unsigned)cached;
}

uint64_t now_us() {
	struct timeval tv;
	gettimeofday(&tv, 0);
	return (uint64_t)tv.tv_sec * 1000000ull + tv.tv_usec;
}

/* How far past a request's cycle the peer may emulate before the transfer is
 * abandoned. A link can legitimately be left half-open - a game that stops
 * listening, or one that never enabled its side - so a bound is needed, and it
 * has to be a bound in emulated time or the two devices give up at different
 * points and diverge.
 *
 * A normal-speed Game Boy transfer completes in about 4096 cycles, so four of
 * those is generous for any real exchange. It also has to be comfortably less
 * than a frame: positions are measured from the start of each paired frame and
 * a frame is only about 70224 cycles, so a larger deadline could never be
 * reached and an unanswerable send would block until the frame ended. */
const uint64_t TRANSFER_DEADLINE_CYCLES = 4096ull * 4ull;

} // namespace

/* Caller holds mutex_. Returns false only if it gave up reporting; the caller
 * always re-tests its own predicate. */
void NetplayLocalSerialBus::waitReporting(const char *who, unsigned endpoint) {
	const unsigned ms = stall_report_ms();
	if (!ms) { pthread_cond_wait(&changed_, &mutex_); return; }

	struct timeval tv;
	gettimeofday(&tv, 0);
	struct timespec deadline;
	deadline.tv_sec = tv.tv_sec + ms / 1000;
	deadline.tv_nsec = (long)(tv.tv_usec * 1000) + (long)(ms % 1000) * 1000000L;
	if (deadline.tv_nsec >= 1000000000L) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000L; }

	if (pthread_cond_timedwait(&changed_, &mutex_, &deadline) == ETIMEDOUT &&
	    !reported_stall_) {
		reported_stall_ = true;
		fprintf(stderr,
			"[local_serial] STALL in %s(ep=%u): "
			"pos=[%llu,%llu] running=[%d,%d] active=[%d,%d] "
			"req=[%d@%llu,%d@%llu] resp=[%d,%d]\n",
			who, endpoint,
			(unsigned long long)pos_[0], (unsigned long long)pos_[1],
			(int)running_[0], (int)running_[1],
			(int)active_[0], (int)active_[1],
			(int)req_active_[0], (unsigned long long)req_at_[0],
			(int)req_active_[1], (unsigned long long)req_at_[1],
			(int)resp_ready_[0], (int)resp_ready_[1]);
		fflush(stderr);
	}
}

NetplayLocalSerialBus::NetplayLocalSerialBus()
: stopped_(false)
, reported_stall_(false)
, generation_(0)
, exchanges_(0)
, send_timeouts_(0)
, poll_timeouts_(0)
, simultaneous_sends_(0)
, total_wait_us_(0)
, longest_wait_us_(0) {
	pthread_mutex_init(&mutex_, 0);
	pthread_cond_init(&changed_, 0);
	active_[0] = active_[1] = false;
	running_[0] = running_[1] = false;
	based_[0] = based_[1] = false;
	base_[0] = base_[1] = 0;
	pos_[0] = pos_[1] = 0;
	for (unsigned e = 0; e < 2; e++) {
		req_active_[e] = false;
		req_byte_[e] = 0xFF;
		req_fast_[e] = false;
		req_at_[e] = 0;
		resp_ready_[e] = false;
		resp_byte_[e] = 0xFF;
	}
}

NetplayLocalSerialBus::~NetplayLocalSerialBus() {
	pthread_mutex_lock(&mutex_);
	stopped_ = true;
	pthread_cond_broadcast(&changed_);
	pthread_mutex_unlock(&mutex_);
	pthread_cond_destroy(&changed_);
	pthread_mutex_destroy(&mutex_);
}

void NetplayLocalSerialBus::reset() {
	pthread_mutex_lock(&mutex_);
	active_[0] = active_[1] = false;
	running_[0] = running_[1] = false;
	based_[0] = based_[1] = false;
	base_[0] = base_[1] = 0;
	pos_[0] = pos_[1] = 0;
	for (unsigned e = 0; e < 2; e++) {
		req_active_[e] = false;
		req_byte_[e] = 0xFF;
		req_fast_[e] = false;
		req_at_[e] = 0;
		resp_ready_[e] = false;
		resp_byte_[e] = 0xFF;
	}
	++generation_;
	exchanges_ = send_timeouts_ = poll_timeouts_ = simultaneous_sends_ = 0;
	total_wait_us_ = 0;
	longest_wait_us_ = 0;
	pthread_cond_broadcast(&changed_);
	pthread_mutex_unlock(&mutex_);
}

void NetplayLocalSerialBus::beginFrame() {
	pthread_mutex_lock(&mutex_);
	/* Rebase both consoles to zero for the coming frame. Their cycle counters
	 * are private and are rebased by savestates, so only positions measured
	 * from a common per-frame origin are comparable between the two. */
	based_[0] = based_[1] = false;
	pos_[0] = pos_[1] = 0;
	/* Both consoles are known to be running this frame, so mark them active
	 * here rather than letting each announce itself as it starts. Otherwise a
	 * console that got going first could see a peer that had not yet announced,
	 * read that as "finished, will never answer", and abandon a transfer the
	 * peer was about to take - a startup race that made the first exchange of a
	 * frame depend on which thread was scheduled first. */
	active_[0] = active_[1] = true;
	running_[0] = running_[1] = true;
	pthread_cond_broadcast(&changed_);
	pthread_mutex_unlock(&mutex_);
}

uint64_t NetplayLocalSerialBus::position(unsigned endpoint) const {
	return pos_[endpoint];
}

void NetplayLocalSerialBus::publish(unsigned endpoint, unsigned long cc) {
	if (!based_[endpoint]) {
		base_[endpoint] = cc;
		based_[endpoint] = true;
		pos_[endpoint] = 0;
		return;
	}
	/* Gambatte's counter is monotonic within a frame; a savestate rebase only
	 * happens between frames, where beginFrame() re-establishes the origin. */
	const uint64_t p = (uint64_t)(cc - base_[endpoint]);
	if (p > pos_[endpoint]) pos_[endpoint] = p;
}

void NetplayLocalSerialBus::advance(unsigned endpoint, unsigned long cc) {
	if (endpoint > 1) return;
	pthread_mutex_lock(&mutex_);
	const uint64_t before = pos_[endpoint];
	publish(endpoint, cc);
	if (pos_[endpoint] != before) pthread_cond_broadcast(&changed_);
	pthread_mutex_unlock(&mutex_);
}

void NetplayLocalSerialBus::setActive(unsigned endpoint, bool active) {
	if (endpoint > 1) return;
	pthread_mutex_lock(&mutex_);
	active_[endpoint] = active;
	pthread_cond_broadcast(&changed_);
	pthread_mutex_unlock(&mutex_);
}

void NetplayLocalSerialBus::setRunning(unsigned endpoint, bool running) {
	if (endpoint > 1) return;
	pthread_mutex_lock(&mutex_);
	running_[endpoint] = running;
	pthread_cond_broadcast(&changed_);
	pthread_mutex_unlock(&mutex_);
}

bool NetplayLocalSerialBus::waitForService(unsigned endpoint) {
	if (endpoint > 1) return false;
	const unsigned peer = endpoint ^ 1;
	pthread_mutex_lock(&mutex_);
	/* Waits on the peer still *running*, not on it still being active. Waiting
	 * on activity deadlocks: both consoles finish, both wait here, and neither
	 * can drop its activity because that happens after this loop. */
	while (!stopped_ && running_[peer] && !req_active_[peer])
		waitReporting("waitForService", endpoint);
	const bool service = !stopped_ && req_active_[peer];
	pthread_mutex_unlock(&mutex_);
	return service;
}

bool NetplayLocalSerialBus::peerActive(unsigned endpoint) {
	if (endpoint > 1) return false;
	pthread_mutex_lock(&mutex_);
	const bool active = active_[endpoint ^ 1];
	pthread_mutex_unlock(&mutex_);
	return active;
}

bool NetplayLocalSerialBus::isIdle() {
	pthread_mutex_lock(&mutex_);
	const bool idle = !stopped_ && !req_active_[0] && !req_active_[1] &&
		!resp_ready_[0] && !resp_ready_[1] && !active_[0] && !active_[1];
	pthread_mutex_unlock(&mutex_);
	return idle;
}

void NetplayLocalSerialBus::snapshot(NetplayLocalSerialStats& stats) {
	pthread_mutex_lock(&mutex_);
	stats.exchanges = exchanges_;
	stats.send_timeouts = send_timeouts_;
	stats.poll_timeouts = poll_timeouts_;
	stats.simultaneous_sends = simultaneous_sends_;
	stats.total_wait_us = total_wait_us_;
	stats.longest_wait_us = longest_wait_us_;
	pthread_mutex_unlock(&mutex_);
}

unsigned char NetplayLocalSerialBus::send(unsigned endpoint, unsigned long cc,
		unsigned char data, bool fastCgb) {
	if (endpoint > 1) return 0xFF;
	const uint64_t started = now_us();
	const unsigned peer = endpoint ^ 1;

	pthread_mutex_lock(&mutex_);
	publish(endpoint, cc);

	req_active_[endpoint] = true;
	req_byte_[endpoint] = data;
	req_fast_[endpoint] = fastCgb;
	req_at_[endpoint] = position(endpoint);
	resp_ready_[endpoint] = false;
	const uint64_t generation = generation_;
	pthread_cond_broadcast(&changed_);

	unsigned char response = 0xFF;
	bool got = false;
	for (;;) {
		if (stopped_ || generation != generation_) break;

		if (req_active_[peer]) {
			if (req_at_[peer] == req_at_[endpoint]) {
				/* Both consoles asserted their own clock at the same emulated
				 * cycle - legal on a real cable, and here simply a symmetric
				 * exchange where each takes the other's byte. No ordering is
				 * involved, so both devices resolve it identically without
				 * anyone having to win a race. */
				response = req_byte_[peer];
				resp_byte_[peer] = data;
				resp_ready_[peer] = true;
				req_active_[peer] = false;
				req_active_[endpoint] = false;
				++simultaneous_sends_;
				got = true;
				pthread_cond_broadcast(&changed_);
				break;
			}
			if (req_at_[peer] > req_at_[endpoint]) {
				/* Ours is the earlier clock and the peer is busy clocking a
				 * later one, so it cannot answer this transfer - on a real
				 * cable the line simply reads idle. Pairing across different
				 * cycles is what let a transfer be answered by a byte from the
				 * future, and which byte arrived depended on thread timing.
				 * Leave unpaired; the peer's own send resolves the same way
				 * from its side. */
				break;
			}
			/* The peer's clock is earlier; let it resolve first. */
		}

		/* The peer consumed our request through check(). */
		if (resp_ready_[endpoint]) {
			response = resp_byte_[endpoint];
			resp_ready_[endpoint] = false;
			req_active_[endpoint] = false;
			got = true;
			break;
		}

		/* Give up only once the peer has emulated past a fixed cycle deadline
		 * without taking it - a bound in emulated time, so both devices abandon
		 * the same transfer at the same emulated moment. An inactive peer has
		 * finished its frame and will take nothing more. */
		if (position(peer) > req_at_[endpoint] + TRANSFER_DEADLINE_CYCLES) break;
		if (!active_[peer]) break;

		pthread_cond_wait(&changed_, &mutex_);
	}

	if (got) {
		++exchanges_;
		const uint64_t waited = now_us() - started;
		total_wait_us_ += waited;
		if (waited > longest_wait_us_)
			longest_wait_us_ = waited > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)waited;
	} else {
		req_active_[endpoint] = false;
		if (!stopped_ && generation == generation_) ++send_timeouts_;
	}
	pthread_cond_broadcast(&changed_);
	pthread_mutex_unlock(&mutex_);
	return response;
}

bool NetplayLocalSerialBus::check(unsigned endpoint, unsigned long cc,
		unsigned char out, unsigned char& in, bool& fastCgb) {
	if (endpoint > 1) return false;
	const unsigned peer = endpoint ^ 1;

	pthread_mutex_lock(&mutex_);
	publish(endpoint, cc);

	for (;;) {
		const uint64_t at = position(endpoint);

		/* Take the peer's request only once this console has emulated as far as
		 * the cycle it was made at, under the same ordering the wait below uses:
		 * endpoint 0 acts first at any given cycle, so a request the peer made
		 * at exactly this cycle is not yet visible to endpoint 0 and is visible
		 * to endpoint 1. Permitting the order without enforcing it here left
		 * *which* check consumed a request decided by arrival: the totals
		 * matched but the emulated states did not. */
		const bool visible = req_active_[peer] &&
			(endpoint == 0 ? req_at_[peer] < at : req_at_[peer] <= at);
		if (visible) {
			in = req_byte_[peer];
			fastCgb = req_fast_[peer];
			resp_byte_[peer] = out;
			resp_ready_[peer] = true;
			req_active_[peer] = false;
			pthread_cond_broadcast(&changed_);
			pthread_mutex_unlock(&mutex_);
			return true;
		}

		if (stopped_) break;

		/* If the peer has not yet emulated *past* this console, it may still
		 * post a request at a cycle already reached here - including at this
		 * exact cycle - and answering "nothing" now is how two devices came to
		 * disagree. Level is not good enough: wait until it is strictly ahead,
		 * at which point no earlier request can still appear. Both consoles are
		 * always advancing, so this resolves; an inactive peer has finished its
		 * frame and will post nothing more. */
		/* Two consoles at the same emulated position need a defined order, or
		 * each waits for the other to move first and neither ever does - which
		 * is exactly where this deadlocked, both sitting at position 0 at the
		 * start of a frame.
		 *
		 * Endpoint 0 is defined to act first at any given cycle. It may
		 * therefore conclude "nothing here" as soon as its peer is level, while
		 * endpoint 1 must wait until its peer has moved past, so that anything
		 * endpoint 0 does at this cycle is already visible. The order is
		 * arbitrary but fixed, which is all determinism requires.
		 *
		 * A peer that has finished its frame has a final position and can post
		 * nothing further, so waiting on it would never return. */
		const bool peer_settled = (endpoint == 0) ? position(peer) >= at
		                                          : position(peer) > at;
		if (!running_[peer] || peer_settled) break;

		waitReporting("check", endpoint);
	}

	++poll_timeouts_;
	pthread_mutex_unlock(&mutex_);
	return false;
}
