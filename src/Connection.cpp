#include "../include/Connection.hpp"
#include <cstddef>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <sstream>

static const time_t CONN_TIMEOUT_SEC = 60;

Connection::Connection(int fd, const ServerConfig* server)
	: _fd(fd), _state(READING), _server(server), _lastActive(time(NULL)) {}

Connection::~Connection() {
	if (_fd >= 0)
		close(_fd);
}

void Connection::onReadable() {
	char buf[4096];
	ssize_t n = recv(_fd, buf, sizeof(buf), 0);

	if (n == 0) { // client closed the connection
		_state = CLOSING;
		return;
	}
	if (n < 0) // no data to read
		return;

	_lastActive = time(NULL);
	// _outBuf.append(buf, static_cast<size_t>(n));

	//step 4
	_inBuf.append(buf, static_cast<size_t>(n));
	if (_inBuf.size() > 8192) {
		_state = CLOSING;
		return;
	}
	// for stub, replace this code later
	if (_inBuf.find("\r\n\r\n") != std::string::npos) {
		buildStubResponse();
		_state = WRITING;
	}
}

void Connection::onWritable() {
	if (_outBuf.empty())
		return;

	ssize_t n = send(_fd, _outBuf.data(), _outBuf.size(), 0);
	if (n < 0)
		return;

	_lastActive = time(NULL);
	_outBuf.erase(0, static_cast<size_t>(n));

	//step4
	if (_outBuf.empty())
		_state = CLOSING;
}

// POLLIN = 0000 0001, POLLOUT = 0000 0100
// POLLIN | POLLOUT = 0000 0101
short Connection::wantedEvents() const {
	// short ev = POLLIN;
	// if (!_outBuf.empty())
	// 	ev |= POLLOUT;
	// return ev;

	//step4
	if (_state == WRITING)
		return POLLOUT;
	return POLLIN;
}

bool Connection::shouldClose() const {
	return _state == CLOSING;
}

bool Connection::isTimedOut(time_t now) const {
	return now - _lastActive > CONN_TIMEOUT_SEC;
}

int Connection::getFd() const {
	return _fd;
}

const ServerConfig* Connection::getServer() const {
	return _server;
}

// step4, for stub
void Connection::buildStubResponse() {
	std::ostringstream body;
	body << "Hello from webserv (port " << _server->port << ")\n";
	std::string b = body.str();

	std::ostringstream res;
	res << "HTTP/1.1 200 OK\r\n"
		<< "Content-Type: text/plain\r\n"
		<< "Content-Length: " << b.size() << "\r\n"
		<< "Connection: close\r\n"
		<< "\r\n"
		<< b;
	_outBuf = res.str();
}