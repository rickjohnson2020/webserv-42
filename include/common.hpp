#ifndef COMMON_HPP
#define COMMON_HPP

#include <string>
#include <vector>
#include <map>
#include <set>
#include <iostream>

#ifdef DEBUG
# define LOG(msg) (std::cout << "[debug] " << msg << std::endl)
#else
# define LOG(msg)
#endif

struct LocationConfig {
	std::string path;
	std::set<std::string> allowedMethods;
	std::string root;
	std::string index;
	bool autoindex;
	std::pair<int, std::string> redirect;
	std::string uploadStore;
	std::map<std::string, std::string> cgi;

	LocationConfig() : autoindex(false), redirect(0, "") {}
};

struct ServerConfig {
	std::string host;
	int port;
	std::map<int, std::string> errorPages;
	size_t clientMaxBodySize;
	std::vector<LocationConfig> locations;

	ServerConfig() : host("0.0.0.0"), port(0),
					 clientMaxBodySize(1024 * 1024) {}
};

enum Method {
	METHOD_GET,
	METHOD_POST,
	METHOD_DELETE,
	METHOD_UNKNOWN
};

enum ConnState {
	READING,
	HANDLING,
	CGI_RUNNING,
	WRITING,
	CLOSING
};

#endif