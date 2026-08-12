#ifndef _NET_SERIAL_H
#define _NET_SERIAL_H

#if defined(__HAIKU__)
#include <sys/socket.h>
#include <sys/select.h>
#endif

#include <gambatte.h>
#include <time.h>
#include <sys/time.h>

class NetSerial : public gambatte::SerialIO
{
	public:
		NetSerial();
		~NetSerial();

		bool start(bool is_server, int port, const std::string& hostname,
		           bool local_link = false);
		void stop();

		virtual bool check(unsigned char out, unsigned char& in, bool& fastCgb);
		virtual unsigned char send(unsigned char data, bool fastCgb);

	private:
		bool startServerSocket();
		bool startClientSocket();
		bool acceptClient();
		bool checkAndRestoreConnection(bool throttle);
		bool writePacket(const unsigned char *data, size_t len);
		bool readPacket(unsigned char *data, size_t len);
		void connected(int fd);
		void reportStats(bool force);

		bool is_stopped_;
		bool is_server_;
		bool local_link_;
		int  port_;
		std::string hostname_;

		int server_fd_;
		int sockfd_;

		clock_t lastConnectAttempt_;
		unsigned long transactions_;
		unsigned long total_wait_us_;
		unsigned long longest_wait_us_;
		unsigned long timeouts_;
		unsigned long reconnects_;
		struct timeval stats_started_;
};

#endif
