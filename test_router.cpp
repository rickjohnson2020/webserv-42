// Standalone test driver for Router::match.
// No sockets, no HttpRequest -- just build a ServerConfig by hand and
// check which LocationConfig gets selected for various request paths.
//
// Build:
//   c++ -Wall -Wextra -Werror -std=c++98 Router.cpp test_router.cpp -o test_router
// Run:
//   ./test_router

#include "Router.hpp"
#include <iostream>

static LocationConfig makeLocation(const std::string& path)
{
    LocationConfig loc;
    loc.path = path;
    loc.autoindex = false;
    loc.redirect = std::make_pair(0, "");
    return loc;
}

static void check(const std::string& label, const LocationConfig* result,
                   const std::string& expectedPath)
{
    std::cout << "--- " << label << " ---" << std::endl;
    if (expectedPath.empty())
    {
        if (result == NULL)
            std::cout << "  PASS: no location matched (NULL), as expected" << std::endl;
        else
            std::cout << "  FAIL: expected NULL, got location \"" << result->path << "\"" << std::endl;
        return;
    }

    if (result == NULL)
    {
        std::cout << "  FAIL: expected \"" << expectedPath << "\", got NULL" << std::endl;
        return;
    }
    if (result->path == expectedPath)
        std::cout << "  PASS: matched \"" << result->path << "\"" << std::endl;
    else
        std::cout << "  FAIL: expected \"" << expectedPath << "\", got \"" << result->path << "\"" << std::endl;
}

int main()
{
    // A server with a root location plus a couple of nested/specific ones,
    // mirroring the example .conf in the design document.
    ServerConfig srv;
    srv.host = "0.0.0.0";
    srv.port = 8080;
    srv.clientMaxBodySize = 1024 * 1024;
    srv.locations.push_back(makeLocation("/"));
    srv.locations.push_back(makeLocation("/upload"));
    srv.locations.push_back(makeLocation("/upload/special"));
    srv.locations.push_back(makeLocation("/cgi-bin"));

    check("Exact match on root",
          Router::match(srv, "/"), "/");

    check("Exact match on /upload",
          Router::match(srv, "/upload"), "/upload");

    check("Nested path under /upload",
          Router::match(srv, "/upload/photo.png"), "/upload");

    check("Longest match preferred (/upload/special beats /upload)",
          Router::match(srv, "/upload/special/report.pdf"), "/upload/special");

    check("cgi-bin path",
          Router::match(srv, "/cgi-bin/hello.py"), "/cgi-bin");

    // The important trap: "/uploader" starts with "/upload" as a plain
    // string, but it is NOT the same path segment. This must fall back
    // to "/", not incorrectly match "/upload".
    check("Boundary trap: /uploader/x must NOT match /upload",
          Router::match(srv, "/uploader/x"), "/");

    check("Unrelated path falls back to root",
          Router::match(srv, "/anything/else"), "/");

    // A server with NO root location configured at all -- an unmatched
    // path here must return NULL so the caller can return 404.
    ServerConfig srvNoRoot;
    srvNoRoot.host = "0.0.0.0";
    srvNoRoot.port = 8081;
    srvNoRoot.clientMaxBodySize = 1024;
    srvNoRoot.locations.push_back(makeLocation("/api"));

    check("No location matches at all -> NULL",
          Router::match(srvNoRoot, "/nope"), "");

    return 0;
}