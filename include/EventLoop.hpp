#ifndef EVENTLOOP_HPP
#define EVENTLOOP_HPP

#include "./ListenSocket.hpp"
#include "./Connection.hpp"
#include <poll.h>
#include <vector>

class EventLoop {
private:
	std::vector<struct pollfd> _pfds;
	std::map<int, ListenSocket*> _listeners;
	std::map<int, Connection*> _conns;
	// std::map<int, CgiProcess*> _cgis;

	EventLoop(const EventLoop&);
	EventLoop& operator=(const EventLoop&);

	void buildPollFds();
	void dispatch();
	void acceptNewClients(ListenSocket* ls);
	void closeConnection(int fd);
	void sweepTimeouts();

public:
	EventLoop();
	~EventLoop();
	void addListener(ListenSocket* ls);
	// void attachCgi(int pipeFd, CgiProcess* p);
	// void detachCgi(int pipeFd);
	void run();
};

#endif