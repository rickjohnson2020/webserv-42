/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Router.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: teppeiyamano <teppeiyamano@student.42.f    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/07/18 17:28:44 by teppeiyaman       #+#    #+#             */
/*   Updated: 2026/07/20 21:50:50 by teppeiyaman      ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Router.hpp"

bool    Router::isPrefixMatch(const std::string& locPath, const std::string& reqPath)
{
    if (reqPath.compare(0, locPath.size(), locPath) != 0)
        return (false);
    if (reqPath.size() == locPath.size())
        return (true);
    if (!locPath.empty() && locPath[locPath.size() - 1] == '/')
        return (true);
    return (reqPath[locPath.size()] == '/');
}

const LocationConfig*   Router::match(const ServerConfig& srv, const std::string& path)
{
    const LocationConfig*   best = NULL;
    size_t                  bestLen = 0;
    
    for (size_t i = 0; i < srv.locations.size(); ++i)
    {
        const LocationConfig&   loc = srv.locations[i];
        if (isPrefixMatch(loc.path, path) && (best == NULL || loc.path.size() > bestLen))
        {
            best = &srv.locations[i];
            bestLen = loc.path.size();
        }
    }
    return (best);
}

