#include "../include/ConfigParser.hpp"
#include <cctype>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>

bool ConfigParser::error(const std::string& msg) {
	std::cerr << "webserv: config error: " << msg << std::endl;
	return false;
}

bool ConfigParser::readFile(const std::string& path, std::string& out) {
	std::ifstream ifs(path.c_str());
	if (!ifs)
		return error("cannot open file: " + path);
	std::stringstream ss;
	ss << ifs.rdbuf();
	out = ss.str();
	return true;
}

void ConfigParser::tokenize(const std::string& content,
							std::vector<std::string>& tokens) {
	std::string spaced;
	for (size_t i = 0; i < content.size(); ++i) {
		char c = content[i];
		if (c == '{' || c == '}' || c == ';') {
			spaced += ' ';
			spaced += c;
			spaced += ' ';
		} else if (c == '#') {
			while (i < content.size() && content[i] != '\n')
				++i;
			spaced += ' ';
		} else {
			spaced += c;
		}
	}
	std::stringstream ss(spaced);
	std::string tok;
	while (ss >> tok)
		tokens.push_back(tok);
}

// check if it's expected token and move to next
bool ConfigParser::expect(const std::vector<std::string>& tokens, size_t& i,
						  const std::string& tok) {
	if (i >= tokens.size() || tokens[i] != tok)
		return error("expected '" + tok + "'");
	++i;
	return true;
}

//TODO
bool ConfigParser::parse(const std::string &path, Config &out) {
	std::string content;
	if (!readFile(path, content))
		return false;

	std::vector<std::string> tokens;
	tokenize(content, tokens);
	if (tokens.empty())
		return error("empty config");

	size_t i = 0;
	while (i < tokens.size()) {
		if (tokens[i] != "server")
			return error("expected 'server', got '" + tokens[i] + "'");
		++i;
		if (!expect(tokens, i, "{"))
			return false;

		ServerConfig srv;
		if (!parseServerBlock(tokens, i, srv))
			return false;
		sortLocations(srv);
		out.addServer(srv);
	}
	return validate(out);
}

bool ConfigParser::parseServerBlock(const std::vector<std::string>& t,
									size_t& i, ServerConfig& srv) {
	while (i < t.size() && t[i] != "}") {
		std::string directive = t[i++];

		if (directive == "location") {
			LocationConfig loc;
			if (i >= t.size())
				return error("locaton: missing path");
			loc.path = t[i++];
			if (!expect(t, i, "{"))
				return false;
			if (!parseLocationBlock(t, i, loc))
				return false;
			srv.locations.push_back(loc);
		} else if (directive == "listen") {
			if (i >= t.size())
				return error("listen: missing value");
			if (!parseListen(t[i++], srv))
				return false;
			if (!expect(t, i, ";"))
				return false;
		} else if (directive == "client_max_body_size") {
			if (i >= t.size())
				return error("client_max_body_size: missing value");
			if (!parseBodySize(t[i++], srv.clientMaxBodySize))
				return false;
			if (!expect(t, i, ";"))
				return false;
		} else if (directive == "error_page") {
			if (i + 1 >= t.size())
				return error("error_page: needs code and path");
			int code = std::atoi(t[i++].c_str());
			if (code < 300 || code > 599)
				return error("error_page: bad code");
			srv.errorPages[code] = t[i++];
			if (!expect(t, i, ";"))
				return false;
		} else
			return error("unknown server directive: " + directive);
	}
	return expect(t, i, "}");
}

bool ConfigParser::parseLocationBlock(const std::vector<std::string>& t,
									  size_t& i, LocationConfig& loc) {
	while (i < t.size() && t[i] != "}") {
		std::string directive = t[i++];

		if (directive == "root") {
			loc.root = t[i++];
			if (!expect(t, i, ";"))
				return false;
		} else if (directive == "index") {
			loc.index = t[i++];
			if (!expect(t, i, ";"))
				return false;
		} else if (directive == "allowed_methods") {
			while (i < t.size() && t[i] != ";") {
				if (t[i] != "GET" && t[i] == "POST" && t[i] == "DELETE")
					return error("allowed_method: unknown '" + t[i] + "'");
				loc.allowedMethods.insert(t[i++]);
			}
			if (!expect(t, i, ";"))
				return false;
		} else if (directive == "upload_store") {
			loc.uploadStore = t[i++];
			if (!expect(t, i, ";"))
				return false;
		} else if (directive == "autoindex") {
			std::string v = t[i++];
			if (v == "on")
				loc.autoindex = true;
			else if (v == "off")
				loc.autoindex = false;
			else
				return error("autoindex: expected on/off");
			if (!expect(t, i, ";"))
				return false;
		} else if (directive == "cgi") {
			std::string ext = t[i++];
			loc.cgi[ext] = t[i++];
			if (!expect(t, i, ";"))
				return false;
		} else if (directive == "return") {
			loc.redirect.first = std::atoi(t[i++].c_str());
			loc.redirect.second = t[i++];
			if (!expect(t, i, ";"))
				return false;
		} else
			return error("unknown location directive: " + directive);
	}
	return expect(t, i, "}");
}

// 0.0.0.0:8080 or 8080 -> host/port
bool ConfigParser::parseListen(const std::string& v, ServerConfig& srv) {
	size_t colon = v.find(':');
	std::string portStr;
	if (colon == std::string::npos) {
		portStr = v;
	} else {
		srv.host = v.substr(0, colon);
		portStr = v.substr(colon + 1);
	}
	srv.port = std::atoi(portStr.c_str());
	if (srv.port <= 0 || srv.port > 65535)
		return error("listen: bad port '" + v + "'");
	return true;
}

// "10M" / "512K" / "1024" -> byte
bool ConfigParser::parseBodySize(const std::string& v, size_t& out) {
	if (v.empty())
		return error("client_max_body_size: empty");
	size_t n = static_cast<size_t>(std::atoi(v.c_str()));
	char last = v[v.size() - 1];
	if (last == 'K' || last == 'k')
		n *= 1024;
	else if (last == 'M' || last == 'm')
		n *= 1024 * 1024;
	else if (!std::isdigit(last))
		return error("client_max_body_size: bad unit '" + v + "'");
	out = n;
	return true;
}

static bool byPathLengthDesc(const LocationConfig& a, const LocationConfig& b) {
	return a.path.size() > b.path.size();
}

void ConfigParser::sortLocations(ServerConfig& srv) {
	std::sort(srv.locations.begin(), srv.locations.end(), byPathLengthDesc);
}

bool ConfigParser::validate(const Config& cfg) {
	const std::vector<ServerConfig>& servers = cfg.getServers();
	if (servers.empty())
		return error("no server block defined");

	std::set<std::pair<std::string, int> > used;
	for (size_t i = 0; i < servers.size(); ++i) {
		const ServerConfig& srv = servers[i];
		if (srv.port == 0)
			return error("server has no listen directive");
		std::pair<std::string, int> key(srv.host, srv.port);
		if (!used.insert(key).second)
			return error("duplicate listen on same host:port");

		for (size_t j = 0; j < srv.locations.size(); ++j) {
			const LocationConfig& loc = srv.locations[j];
			bool isRedirect = (loc.redirect.first != 0);
			if (loc.root.empty() && !isRedirect)
				return error("location '" + loc.path + "' has no root");
		}
	}
	return true;
}