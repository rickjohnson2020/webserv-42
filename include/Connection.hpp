#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "./common.hpp"
#include <ctime>
#include <string>
#include <poll.h>

class Connection {
private:
	int _fd;
	ConnState _state;
	std::string _inBuf;
	std::string _outBuf;
	// HttpRequest _req;
	const ServerConfig* _server;
	time_t _lastActive;

	Connection(const Connection&);
	Connection& operator=(const Connection&);

	//step 4, for stub
	void buildStubResponse();

public:
	Connection(int fd, const ServerConfig* server);
	~Connection();

	void onReadable();
	void onWritable();
	// void onCgiDone(const HttpResponse& r);
	short wantedEvents() const;
	bool shouldClose() const;
	bool isTimedOut(time_t now) const;

	int getFd() const;
	const ServerConfig* getServer() const;

};


#endif