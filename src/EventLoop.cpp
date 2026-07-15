#include "../include/EventLoop.hpp"
#include <csignal>
#include <cstddef>
#include <poll.h>
#include <vector>
#include <ctime>

static volatile sig_atomic_t g_running = 1;

static void handleSigint(int) {
	g_running = 0;
}

EventLoop::EventLoop() {
	signal(SIGINT, handleSigint);
	signal(SIGPIPE, SIG_IGN);
}

EventLoop::~EventLoop() {
	for (std::map<int, Connection*>::iterator it = _conns.begin();
		 it != _conns.end(); ++it)
		delete it->second;
	_conns.clear();
}

void EventLoop::addListener(ListenSocket* ls) {
	_listeners[ls->getFd()] = ls;
}

void EventLoop::buildPollFds() {
	_pfds.clear();
	struct pollfd p;
	p.revents = 0;

	for (std::map<int, ListenSocket*>::iterator it = _listeners.begin();
		 it != _listeners.end(); ++it) {
		p.fd = it->first;
		p.events = POLLIN;
		_pfds.push_back(p);
	}
	for (std::map<int, Connection*>::iterator it = _conns.begin();
		 it != _conns.end(); ++it) {
		p.fd = it->first;
		p.events = it->second->wantedEvents();
		_pfds.push_back(p);
	}
}

void EventLoop::dispatch() {
	std::vector<int> toClose;

	for (size_t i = 0; i < _pfds.size(); ++i) {
		int fd = _pfds[i].fd;
		short revents = _pfds[i].revents;
		if (revents == 0)
			continue;

		std::map<int, ListenSocket*>::iterator lit = _listeners.find(fd);
		if (lit != _listeners.end()) {
			if (revents & POLLIN)
				acceptNewClients(lit->second);
			continue;
		}

		std::map<int, Connection*>::iterator cit = _conns.find(fd);
		if (cit == _conns.end())
			continue;
		Connection* conn = cit->second;

		LOG("fd=" << fd << " revents=" << revents
            << " IN="  << ((revents & POLLIN)  ? 1 : 0)
            << " OUT=" << ((revents & POLLOUT) ? 1 : 0)
            << " HUP=" << ((revents & POLLHUP) ? 1 : 0)
            << " ERR=" << ((revents & POLLERR) ? 1 : 0));

		if (revents & (POLLERR | POLLNVAL)) {
			LOG("fd=" << fd << " -> close path (before read)");
			toClose.push_back(fd);
			continue;
		}

		if (revents & (POLLIN | POLLHUP)) {
			conn->onReadable();
			LOG("fd=" << fd << " after onReadable, shouldClose="
					<< conn->shouldClose());
		}
		if (revents & POLLOUT)
			conn->onWritable();

		if (conn->shouldClose())
			toClose.push_back(fd);
	}

	for (size_t i = 0; i < toClose.size(); ++i) {
		closeConnection(toClose[i]);
	}
}

void EventLoop::acceptNewClients(ListenSocket* ls) {
	while (true) {
		int cfd = ls->acceptClient();
		if (cfd < 0)
			break;
		_conns[cfd] = new Connection(cfd, ls->getServer());
	}
}

void EventLoop::closeConnection(int fd) {
	std::map<int, Connection*>::iterator it = _conns.find(fd);
	if (it == _conns.end())
		return;
	delete it->second;
	_conns.erase(it);
}

void EventLoop::sweepTimeouts() {
	time_t now = time(NULL);
	std::vector<int> toClose;
	for (std::map<int, Connection*>::iterator it = _conns.begin();
		 it != _conns.end(); ++it) {
		if (it->second->isTimedOut(now))
			toClose.push_back(it->first);
	}
	for (size_t i = 0; i < toClose.size(); ++i)
		closeConnection(toClose[i]);
}

void EventLoop::run() {
	while (g_running) {
		buildPollFds();
		if (_pfds.empty())
			break;

		int ready = poll(&_pfds[0], _pfds.size(), 1000);
		if (ready < 0)
			continue;
		if (ready > 0)
			dispatch();

		sweepTimeouts();
	}
}