#ifndef CONFIG_HPP
#define CONFIG_HPP

#include "./common.hpp"

class Config {
private:
	std::vector<ServerConfig> _servers;

public:
	void addServer(const ServerConfig& srv);
	const std::vector<ServerConfig>& getServers() const;
	const ServerConfig* findServer(const std::string& host, int port) const;
};

#endif