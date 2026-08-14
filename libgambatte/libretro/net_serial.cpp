#include "net_serial.h"
#include "libretro.h"
#include "gambatte_log.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#endif

#ifndef _WIN32
static void netserial_configure_socket(int fd)
{
	int one = 1;
	struct timeval tv;
	tv.tv_sec = 2;
	tv.tv_usec = 0;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}
#else
static void netserial_configure_socket(int fd) { (void)fd; }
#endif

static unsigned long elapsed_us(const struct timeval& start)
{
	struct timeval now;
	gettimeofday(&now, NULL);
	long sec = now.tv_sec - start.tv_sec;
	long usec = now.tv_usec - start.tv_usec;
	long total = sec * 1000000L + usec;
	return total > 0 ? (unsigned long)total : 0;
}

NetSerial::NetSerial()
: is_stopped_(true)
, is_server_(false)
, local_link_(false)
, port_(12345)
, hostname_()
, server_fd_(-1)
, sockfd_(-1)
, lastConnectAttempt_(0)
, transactions_(0)
, total_wait_us_(0)
, longest_wait_us_(0)
, timeouts_(0)
, reconnects_(0)
{
	gettimeofday(&stats_started_, NULL);
}

NetSerial::~NetSerial()
{
	stop();
}

bool NetSerial::start(bool is_server, int port, const std::string& hostname,
		bool local_link)
{
	/* Frontends report one global "variables updated" flag. Gambatte used to
	 * tear down Game Link for every unrelated option change, including DMG
	 * colorization. Preserve a healthy link unless its actual configuration
	 * changed. */
	if (!is_stopped_ && is_server_ == is_server && port_ == port &&
	    hostname_ == hostname && local_link_ == local_link)
		return true;
	stop();

	gambatte_log(RETRO_LOG_INFO, "Starting GameLink network %s on %s:%d\n",
			is_server ? "server" : "client", hostname.c_str(), port);
	is_server_ = is_server;
	local_link_ = local_link;
	port_ = port;
	hostname_ = hostname;
	is_stopped_ = false;

	return checkAndRestoreConnection(false);
}
void NetSerial::stop()
{
	if (!is_stopped_) {
		reportStats(true);
		gambatte_log(RETRO_LOG_INFO, "Stopping GameLink network\n");
		is_stopped_ = true;
		if (sockfd_ >= 0) {
			close(sockfd_);
			sockfd_ = -1;
		}
		if (server_fd_ >= 0) {
			close(server_fd_);
			server_fd_ = -1;
		}
	}
}

void NetSerial::connected(int fd)
{
	netserial_configure_socket(fd);
	reconnects_++;
}

void NetSerial::reportStats(bool force)
{
	unsigned long age = elapsed_us(stats_started_);
	if (!force && age < 10000000UL) return;
	if (transactions_ || timeouts_)
		gambatte_log(RETRO_LOG_INFO,
			"GameLink pacing: %lu exchanges, avg %luus, max %luus, "
			"timeouts %lu, connections %lu\n",
			transactions_, transactions_ ? total_wait_us_ / transactions_ : 0,
			longest_wait_us_, timeouts_, reconnects_);
	transactions_ = total_wait_us_ = longest_wait_us_ = timeouts_ = 0;
	reconnects_ = 0;
	gettimeofday(&stats_started_, NULL);
}

bool NetSerial::writePacket(const unsigned char *data, size_t len)
{
	size_t done = 0;
	while (done < len) {
#ifdef _WIN32
		int n = ::send(sockfd_, (const char *)data + done, (int)(len - done), 0);
#else
		ssize_t n = ::send(sockfd_, data + done, len - done, MSG_NOSIGNAL);
#endif
		if (n > 0) { done += (size_t)n; continue; }
		if (n < 0 && errno == EINTR) continue;
		if (n == 0) errno = ECONNRESET;
		return false;
	}
	return true;
}

bool NetSerial::readPacket(unsigned char *data, size_t len)
{
	size_t done = 0;
	struct timeval started;
	gettimeofday(&started, NULL);
	while (done < len) {
#ifdef _WIN32
		int n = recv(sockfd_, (char *)data + done, (int)(len - done), 0);
#else
		ssize_t n = recv(sockfd_, data + done, len - done, 0);
#endif
		if (n > 0) { done += (size_t)n; continue; }
		if (n < 0 && errno == EINTR) continue;
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) timeouts_++;
		if (n == 0) errno = ECONNRESET;
		return false;
	}
	unsigned long waited = elapsed_us(started);
	transactions_++;
	total_wait_us_ += waited;
	if (waited > longest_wait_us_) longest_wait_us_ = waited;
	reportStats(false);
	return true;
}

bool NetSerial::checkAndRestoreConnection(bool throttle)
{
	if (is_stopped_) {
		return false;
	}
	if (sockfd_ < 0 && throttle) {
		clock_t now = clock();
		// Only attempt to establish the connection every 5 seconds
		if (((now - lastConnectAttempt_) / CLOCKS_PER_SEC) < 5) {
			return false;
		}
	}
	lastConnectAttempt_ = clock();
	if (is_server_) {
		if (!startServerSocket()) {
			return false;
		}
		if (!acceptClient()) {
			return false;
		}
	} else {
		if (!startClientSocket()) {
			return false;
		}
	}
	return true;
}
bool NetSerial::startServerSocket()
{
	struct sockaddr_in server_addr;

	if (server_fd_ < 0) {
		memset((char *)&server_addr, '\0', sizeof(server_addr));
		server_addr.sin_family = AF_INET;
		server_addr.sin_port = htons(port_);
		server_addr.sin_addr.s_addr = INADDR_ANY;

		int fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0) {
			gambatte_log(RETRO_LOG_ERROR, "Error opening socket: %s\n", strerror(errno));
			return false;
		}

		int one = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
		if (bind(fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
			gambatte_log(RETRO_LOG_ERROR, "Error on binding: %s\n", strerror(errno));
			close(fd);
			return false;
		}

		if (listen(fd, 1) < 0) {
			gambatte_log(RETRO_LOG_ERROR, "Error listening: %s\n", strerror(errno));
			close(fd);
			return false;
		}
		server_fd_ = fd;
		gambatte_log(RETRO_LOG_INFO, "GameLink network server started!\n");
	}

	return true;
}
bool NetSerial::acceptClient()
{
	struct sockaddr_in client_addr;
	struct timeval tv;
	fd_set rfds;

	if (server_fd_ < 0) {
		return false;
	}
	if (sockfd_ < 0) {
		int retval;

		FD_ZERO(&rfds);
		FD_SET(server_fd_, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 0;

		if (select(server_fd_ + 1, &rfds, NULL, NULL, &tv) <= 0) {
			return false;
		}

		socklen_t client_len = sizeof(client_addr);
		sockfd_ = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
		if (sockfd_ >= 0) connected(sockfd_);
		if (sockfd_ < 0) {
			gambatte_log(RETRO_LOG_ERROR, "Error on accept: %s\n", strerror(errno));
			return false;
		}
		gambatte_log(RETRO_LOG_INFO, "GameLink network server connected to client!\n");
	}
	return true;
}
bool NetSerial::startClientSocket()
{
	struct sockaddr_in server_addr;

	if (sockfd_ < 0) {
		memset((char *)&server_addr, '\0', sizeof(server_addr));
		server_addr.sin_family = AF_INET;
		server_addr.sin_port = htons(port_);

		int fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0) {
			gambatte_log(RETRO_LOG_ERROR, "Error opening socket: %s\n", strerror(errno));
			return false;
		}

		struct hostent* server_hostname = gethostbyname(hostname_.c_str());
		if (server_hostname == NULL) {
			gambatte_log(RETRO_LOG_ERROR, "Error, no such host: %s\n", hostname_.c_str());
			close(fd);
			return false;
		}

		memmove((char*)&server_addr.sin_addr.s_addr, (char*)server_hostname->h_addr, server_hostname->h_length);
		if (connect(fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
			gambatte_log(RETRO_LOG_ERROR, "Error connecting to server: %s\n", strerror(errno));
			close(fd);
			return false;
		}
		sockfd_ = fd;
		connected(sockfd_);
		gambatte_log(RETRO_LOG_INFO, "GameLink network client connected to server!\n");
	}
	return true;
}

unsigned char NetSerial::send(unsigned long /*cc*/, unsigned char data, bool fastCgb)
{
	unsigned char buffer[2];

	if (is_stopped_) {
		return 0xFF;
	}
	if (sockfd_ < 0) {
		if (!checkAndRestoreConnection(true)) {
			return 0xFF;
		}
	}

	buffer[0] = data;
	buffer[1] = fastCgb;
	if (!writePacket(buffer, sizeof(buffer)))
   {
		gambatte_log(RETRO_LOG_ERROR, "Error writing to socket: %s\n", strerror(errno));
		close(sockfd_);
		sockfd_ = -1;
		return 0xFF;
	}

	if (!readPacket(buffer, sizeof(buffer)))
   {
		gambatte_log(RETRO_LOG_ERROR, "Error reading from socket: %s\n", strerror(errno));
		close(sockfd_);
		sockfd_ = -1;
		return 0xFF;
	}

	return buffer[0];
}

bool NetSerial::check(unsigned long /*cc*/, unsigned char out, unsigned char& in, bool& fastCgb)
{
	unsigned char buffer[2];
#ifdef _WIN32
	u_long bytes_avail = 0;
#else
	int bytes_avail = 0;
#endif
	if (is_stopped_) {
		return false;
	}
	if (sockfd_ < 0) {
		if (!checkAndRestoreConnection(true)) {
			return false;
		}
	}
#ifdef _WIN32
   if (ioctlsocket(sockfd_, FIONREAD, &bytes_avail) < 0)
#else
	if (ioctl(sockfd_, FIONREAD, &bytes_avail) < 0)
#endif
   {
		gambatte_log(RETRO_LOG_ERROR, "IOCTL Failed: %s\n", strerror(errno));
		return false;
	}

	// A local paired instance runs concurrently in this process. Give its
	// serial master a brief rendezvous window before advancing the slave past
	// this emulated cycle. The network path must remain non-blocking: waiting
	// there is the latency problem instancing is intended to remove.
	if (bytes_avail < 2 && local_link_) {
#ifndef _WIN32
		struct pollfd pfd;
		pfd.fd = sockfd_;
		pfd.events = POLLIN;
		pfd.revents = 0;
		if (poll(&pfd, 1, 2) > 0 && (pfd.revents & POLLIN))
			ioctl(sockfd_, FIONREAD, &bytes_avail);
#endif
	}
	if (bytes_avail < 2) return false;

	if (!readPacket(buffer, sizeof(buffer)))
   {
		gambatte_log(RETRO_LOG_ERROR, "Error reading from socket: %s\n", strerror(errno));
		close(sockfd_);
		sockfd_ = -1;
		return false;
	}

//	slave_txn_cnt++;

	in = buffer[0];
	fastCgb = buffer[1];

	buffer[0] = out;
	buffer[1] = 128;
	if (!writePacket(buffer, sizeof(buffer)))
   {
		gambatte_log(RETRO_LOG_ERROR, "Error writing to socket: %s\n", strerror(errno));
		close(sockfd_);
		sockfd_ = -1;
		return false;
	}

	return true;
}
