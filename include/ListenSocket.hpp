#ifndef LISTENSOCKET_HPP
#define LISTENSOCKET_HPP

#include "common.hpp"

class ListenSocket {
private:
	int _fd;
	const ServerConfig* _server;
	bool fail(const std::string& msg);

	ListenSocket(const ListenSocket&);
	ListenSocket& operator=(const ListenSocket&);

public:
	ListenSocket();
	~ListenSocket();

	bool setup(const ServerConfig& srv);
	int acceptClient();
	int getFd() const;
	const ServerConfig* getServer() const;
};

#endif