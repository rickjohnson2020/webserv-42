// Standalone test driver for HttpRequest.
// No sockets needed -- feed strings directly into consume(), the same way
// Connection::onReadable() will eventually do with bytes from recv().
//
// Build:
//   c++ -Wall -Wextra -Werror -std=c++98 HttpRequest.cpp test_http_request.cpp -o test_http_request
// Run:
//   ./test_http_request

#include "Httprequest.hpp"
#include <iostream>

static const char* methodName(Method m)
{
    switch (m)
    {
        case METHOD_GET:    return "GET";
        case METHOD_POST:   return "POST";
        case METHOD_DELETE: return "DELETE";
        default:            return "UNKNOWN";
    }
}

static void printResult(const std::string& label, HttpRequest& req)
{
    std::cout << "--- " << label << " ---" << std::endl;
    if (req.hasError())
    {
        std::cout << "  -> PARSE_ERROR" << std::endl;
        return;
    }
    if (!req.isComplete())
    {
        std::cout << "  -> not complete yet (waiting for more bytes)" << std::endl;
        return;
    }
    std::cout << "  method : " << methodName(req.method()) << std::endl;
    std::cout << "  path   : " << req.path() << std::endl;
    std::cout << "  query  : " << req.query() << std::endl;
    std::cout << "  host   : " << req.header("Host") << std::endl;
    std::cout << "  body   : [" << req.body() << "]" << std::endl;
    std::cout << "  body.size() = " << req.body().size() << std::endl;
}

// Compares the actual body against what we expect, character by character,
// so a bug like extra/missing bytes is obvious instead of hidden in a
// terminal that may or may not show \r\n visibly.
static void checkBody(const std::string& label, HttpRequest& req, const std::string& expected)
{
    std::cout << "--- " << label << " ---" << std::endl;
    if (!req.isComplete())
    {
        std::cout << "  FAIL: request did not complete" << std::endl;
        return;
    }
    if (req.body() == expected)
    {
        std::cout << "  PASS: body == \"" << expected << "\" (size "
                   << req.body().size() << ")" << std::endl;
    }
    else
    {
        std::cout << "  FAIL: body size = " << req.body().size()
                   << ", expected size = " << expected.size() << std::endl;
        std::cout << "  actual   (escaped): ";
        for (size_t i = 0; i < req.body().size(); ++i)
        {
            char c = req.body()[i];
            if (c == '\r') std::cout << "\\r";
            else if (c == '\n') std::cout << "\\n";
            else std::cout << c;
        }
        std::cout << std::endl;
        std::cout << "  expected (escaped): ";
        for (size_t i = 0; i < expected.size(); ++i)
        {
            char c = expected[i];
            if (c == '\r') std::cout << "\\r";
            else if (c == '\n') std::cout << "\\n";
            else std::cout << c;
        }
        std::cout << std::endl;
    }
}

int main()
{
    // 1. Simple GET, arrives in one piece
    {
        HttpRequest req;
        req.consume(
            "GET /index.html?x=1 HTTP/1.1\r\n"
            "Host: localhost:8080\r\n"
            "\r\n");
        printResult("Simple GET", req);
    }

    // 2. Same request, delivered one byte at a time (simulates real recv() splitting)
    {
        HttpRequest req;
        std::string raw =
            "GET /a/b/c HTTP/1.1\r\n"
            "Host: example.com\r\n"
            "\r\n";
        for (size_t i = 0; i < raw.size(); ++i)
            req.consume(raw.substr(i, 1));
        printResult("GET fed one byte at a time", req);
    }

    // 3. POST with Content-Length, body split across two consume() calls
    {
        HttpRequest req;
        req.consume(
            "POST /upload HTTP/1.1\r\n"
            "Host: example.com\r\n"
            "Content-Length: 11\r\n"
            "\r\n"
            "hello ");
        printResult("POST (before full body arrives)", req);
        req.consume("world");
        checkBody("POST (after full body arrives)", req, "hello world");
    }

    // 4. Chunked body -- this is the one to watch closely
    {
        HttpRequest req;
        req.consume(
            "POST /upload HTTP/1.1\r\n"
            "Host: example.com\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n"
            "5\r\nhello\r\n"
            "6\r\n world\r\n"
            "0\r\n\r\n");
        checkBody("Chunked POST", req, "hello world");
    }

    // 4b. Chunked body with three chunks, to make any per-chunk leftover bytes obvious
    {
        HttpRequest req;
        req.consume(
            "POST /upload HTTP/1.1\r\n"
            "Host: example.com\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n"
            "3\r\nfoo\r\n"
            "3\r\nbar\r\n"
            "3\r\nbaz\r\n"
            "0\r\n\r\n");
        checkBody("Chunked POST, 3 chunks", req, "foobarbaz");
    }

    // 5. Missing Host header -> must be rejected (HTTP/1.1 requires it)
    {
        HttpRequest req;
        req.consume(
            "GET / HTTP/1.1\r\n"
            "\r\n");
        printResult("Missing Host header", req);
    }

    // 6. reset() then a second request on the same connection (keep-alive)
    {
        HttpRequest req;
        req.consume(
            "GET /first HTTP/1.1\r\n"
            "Host: example.com\r\n"
            "\r\n");
        printResult("Keep-alive: first request", req);
        req.reset();
        req.consume(
            "GET /second HTTP/1.1\r\n"
            "Host: example.com\r\n"
            "\r\n");
        printResult("Keep-alive: second request after reset()", req);
    }

    return 0;
}