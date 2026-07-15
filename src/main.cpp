#include <cstddef>
#include <exception>
#include <iostream>
#include <vector>
#include "../include/ConfigParser.hpp"
#include "../include/ListenSocket.hpp"
#include "../include/Connection.hpp"
#include "../include/EventLoop.hpp"

#include <poll.h>
#include <unistd.h>

namespace {
	struct SocketHolder {
		std::vector<ListenSocket*> items;
		~SocketHolder() {
			for (size_t i = 0; i < items.size(); ++i)
				delete items[i];
		}
	};
}

int main(int argc, char** argv) {
	if (argc != 2) {
		std::cerr << "usage: ./webserv <conf>" << std::endl;
		return 1;
	}
	try {
		Config cfg;
		if (!ConfigParser::parse(argv[1], cfg))
			return 1;

		const std::vector<ServerConfig>& servers = cfg.getServers();
		SocketHolder sockets;
		EventLoop loop;
		for (size_t i = 0; i < servers.size(); ++i) {
			ListenSocket* ls = new ListenSocket();
			if (!ls->setup(servers[i])) {
				delete ls;
				return 1;
			}
			sockets.items.push_back(ls);
			loop.addListener(ls);
		}
		std::cout << "listening on " << servers.size() << " port(s)" << std::endl;
		loop.run();
		std::cout << "shutting down" << std::endl;
	} catch (const std::exception& e) {
		std::cerr << "fatal: " << e.what() << std::endl;
		return 1;
	} catch (...) {
		return 1;
	}
	return 0;
}