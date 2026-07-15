#ifndef CONFIGPARSER_HPP
#define CONFIGPARSER_HPP

#include "./Config.hpp"
#include "common.hpp"
#include <cstddef>

class ConfigParser {
public:
	static bool parse(const std::string& path, Config& out);

private:
	static bool readFile(const std::string& path, std::string& out);
	static void tokenize(const std::string& content,
						 std::vector<std::string>& tokens);
	static bool parseLocationBlock(const std::vector<std::string>& t,
								   size_t& i, LocationConfig& loc);
	static bool parseServerBlock(const std::vector<std::string>& t,
								 size_t& i, ServerConfig& srv);
	static bool parseListen(const std::string& v, ServerConfig& srv);
	static bool parseBodySize(const std::string& v, size_t& out);
	static bool expect(const std::vector<std::string>& t, size_t& i,
					   const std::string& tok);
	static void sortLocations(ServerConfig& srv);
	static bool validate(const Config& cfg);
	static bool error(const std::string& msg);
};

#endif