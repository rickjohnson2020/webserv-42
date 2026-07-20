/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Router.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: teppeiyamano <teppeiyamano@student.42.f    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/07/18 17:03:23 by teppeiyaman       #+#    #+#             */
/*   Updated: 2026/07/20 21:50:49 by teppeiyaman      ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef ROUTER_HPP
# define ROUTER_HPP

# include <string>
# include <vector>
# include <set>
# include <map>
# include <utility>

struct LocationConfig
{
    std::string                         path;
    std::set<std::string>               allowedMethods;
    std::string                         root;
    std::string                         index;
    bool                                autoindex;
    std::pair<int, std::string>         redirect;
    std::string                         uploadStore;
    std::map<std::string, std::string>  cgi;
};

struct ServerConfig
{
    std::string                 host;
    int                         port;
    std::map<int, std::string>  errorPages;
    size_t                      clientMaxBodySize;
    std::vector<LocationConfig> locations;
};

class Router
{
private:
    static bool isPrefixMatch(const std::string& locPath, const std::string& reqPath);
public:
    static const LocationConfig*    match(const ServerConfig& srv, const std::string& path);
};

#endif
