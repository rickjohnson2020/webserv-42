/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Httprequest.hpp                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: teppeiyamano <teppeiyamano@student.42.f    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/07/05 19:25:48 by teppeiyaman       #+#    #+#             */
/*   Updated: 2026/07/18 16:37:20 by teppeiyaman      ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef HTTP_REQUEST_HPP
# define HTTP_REQUEST_HPP

# include <string>
# include <map>
# include <cstddef>

enum    Method
{
    METHOD_GET,
    METHOD_POST,
    METHOD_DELETE,
    METHOD_UNKNOWN
};

enum    ParseState
{
    PARSE_REQUEST_LINE,
    PARSE_HEADERS,
    PARSE_BODY,
    PARSE_CHUNKED,
    PARSE_COMPLETE,
    PARSE_ERROR
};

class   HttpRequest
{
    Method                              _method;
    std::string                         _target;
    std::string                         _path;
    std::string                         _query;
    std::map<std::string, std::string>  _headers;
    std::string                         _body;
    ParseState                          _state;

    std::string                         _buffer;
    bool                                _hasContentLength;
    size_t                              _contentLength;
    bool                                _isChunked;
    size_t                              _chunkBytesRemaining;
    int                                 _chunkSubState; // 0=size line, 1=chunk data, 2=trailer headers
    
    static const size_t                 MAX_REQUEST_LINE_LEN = 8192;
    static const size_t                 MAX_HEADERS_TOTAL_LEN = 16384;
    
public:
    HttpRequest();
    void                consume(const std::string& bytes);
    bool                isComplete() const; // true if _state == PARSE_COMPLETE
    bool                hasError() const; // true if _state == PARSE_ERROR
    
    Method              method() const;
    const std::string&  path() const;
    const std::string&  query() const;
    const std::string&  body() const;
    std::string         header(const std::string& name) const;
    void                reset();

private:
    bool    parseRequestLine();
    bool    parseHeaders();
    bool    finishHeaders();
    bool    parseBody();
    bool    parseChunked();

    static std::string  toLower(const std::string& s);
    static std::string  trim(const std::string& s);
    static Method       stringToMethod(const std::string& s);
};

#endif