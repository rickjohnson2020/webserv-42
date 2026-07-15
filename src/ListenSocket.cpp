#include "../include/ListenSocket.hpp"
#include <cstring>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <iostream>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>

ListenSocket::ListenSocket() : _fd(-1), _server(NULL) {}

ListenSocket::~ListenSocket() {
	if (_fd >= 0)
		close(_fd);
}

bool ListenSocket::fail(const std::string& msg) {
	std::cerr << msg << std::endl;
	if (_fd >= 0) {
		close(_fd);
		_fd = -1;
	}
	return false;
}

// socket -> setsockopt -> fcntl -> bind -> listen
bool ListenSocket::setup(const ServerConfig& srv) {
	_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (_fd < 0)
		return fail("socket() failed");

	int opt = 1;
	if (setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
		return fail("setsockopt() failed");

	if (fcntl(_fd, F_SETFL, O_NONBLOCK) < 0)
		return fail("fcntl() failed");

	// struct sockaddr_in addr;
	// std::memset(&addr, 0, sizeof(addr));
	// addr.sin_family = AF_INET;
	// addr.sin_addr.s_addr = htonl(INADDR_ANY);
	// addr.sin_port = htons(static_cast<unsigned short>(srv.port));

	// if (bind(_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
	// 	std::ostringstream oss;
	// 	oss << "bind() failed on port " << srv.port;
	// 	return fail(oss.str());
	// }

	struct addrinfo hints, *res;
	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	std::ostringstream port;
	port << srv.port;
	if (getaddrinfo(srv.host.c_str(), port.str().c_str(), &hints, &res) != 0)
		return fail("getaddrinfo() failed for " + srv.host);

	int r = bind(_fd, res->ai_addr, res->ai_addrlen);
	freeaddrinfo(res);
	if (r < 0)
		return fail("bind() failed for " + srv.host);

	if (listen(_fd, SOMAXCONN) < 0)
		return fail("listen() failed");

	_server = &srv;

	return true;
}

int ListenSocket::acceptClient() {
	int clientFd = accept(_fd, NULL, NULL);
	if (clientFd < 0)
		return -1;

	if (fcntl(clientFd, F_SETFL, O_NONBLOCK) < 0) {
		close(clientFd);
		return -1;
	}
	return clientFd;
}

int ListenSocket::getFd() const {
	return _fd;
}

const ServerConfig* ListenSocket::getServer() const {
	return _server;
}