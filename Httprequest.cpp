/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Httprequest.cpp                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: teppeiyamano <teppeiyamano@student.42.f    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/07/06 19:20:05 by teppeiyaman       #+#    #+#             */
/*   Updated: 2026/07/20 21:50:53 by teppeiyaman      ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Httprequest.hpp"
#include <cstdlib>
#include <cctype>

HttpRequest::HttpRequest() 
    : _method(METHOD_UNKNOWN),
    _state(PARSE_REQUEST_LINE),
    _hasContentLength(false),
    _contentLength(0),
    _isChunked(false),
    _chunkBytesRemaining(0),
    _chunkSubState(0)
{   
}

std::string HttpRequest::toLower(const std::string& s)
{
    std::string out(s);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
    return (out);
}

std::string HttpRequest::trim(const std::string& s)
{
    size_t  start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
        ++start;
    size_t  end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
        --end;
    return (s.substr(start, end - start));
}

Method  HttpRequest::stringToMethod(const std::string& s)
{
    if (s == "GET")
        return (METHOD_GET);
    if (s == "POST")
        return (METHOD_POST);
    if (s == "DELETE")
        return (METHOD_DELETE);
    return (METHOD_UNKNOWN);
}

void    HttpRequest::consume(const std::string& bytes)
{
    if (_state == PARSE_COMPLETE || _state == PARSE_ERROR)
        return;
    
    _buffer.append(bytes);
    
    bool    progressed = true;
    while (progressed && _state != PARSE_COMPLETE && _state != PARSE_ERROR)
    {
        switch (_state)
        {
            case PARSE_REQUEST_LINE:
                progressed = parseRequestLine();
                break;
            case PARSE_HEADERS:
                progressed = parseHeaders();
                break;
            case PARSE_BODY:
                progressed = parseBody();
                break;
            case PARSE_CHUNKED:
                progressed = parseChunked();
                break;
            default:
                progressed = false;
                break;
        }
    }
}

bool HttpRequest::parseRequestLine()
{
    size_t  pos = _buffer.find("\r\n");
    if (pos == std::string::npos)
    {
        if (_buffer.size() > MAX_REQUEST_LINE_LEN)
        {
            _state = PARSE_ERROR;
            return (true);
        }
        return (false); // wait for more bytes
    }
    std::string line = _buffer.substr(0, pos);
    _buffer.erase(0, pos + 2);
    
    size_t  sp1 = line.find(' ');
    size_t  sp2 = (sp1 == std::string::npos) ? std::string::npos : line.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos)
    {
        _state = PARSE_ERROR;
        return (true);
    }
    std::string methodStr = line.substr(0, sp1);
    _target = line.substr(sp1 + 1, sp2 - sp1 - 1);
    std::string version = line.substr(sp2 + 1);
    if (version.compare(0, 7, "HTTP/1.") != 0 || _target.empty())
    {
        _state = PARSE_ERROR;
        return (true);
    }
    _method = stringToMethod(methodStr);
    
    size_t  qpos = _target.find('?');
    if (qpos == std::string::npos)
    {
        _path = _target;
        _query.clear();
    }
    else
    {
        _path = _target.substr(0, qpos);
        _query = _target.substr(qpos + 1);
    }
    if (_path.empty() || _path[0] != '/')
    {
        _state = PARSE_ERROR;
        return (true);
    }
    _state = PARSE_HEADERS;
    return (true);
}

bool    HttpRequest::parseHeaders()
{
    size_t  pos = _buffer.find("\r\n");
    if (pos == std::string::npos)
    {
        if (_buffer.size() > MAX_HEADERS_TOTAL_LEN)
        {
            _state = PARSE_ERROR;
            return (true);
        }
        return (false);
    }
    std::string line = _buffer.substr(0, pos);
    _buffer.erase(0, pos + 2);
    if (line.empty())
        return (finishHeaders());
    
    size_t  colon = line.find(':');
    if (colon == std::string::npos)
    {
        _state = PARSE_ERROR;
        return (true);
    }
    std::string key = toLower(trim(line.substr(0, colon)));
    std::string value = trim(line.substr(colon + 1));
    if (key.empty())
    {
        _state = PARSE_ERROR;
        return (true);
    }
    _headers[key] = value;
    return (true);
}

bool    HttpRequest::finishHeaders()
{
    if (_headers.find("host") == _headers.end())
    {
        _state = PARSE_ERROR;
        return (true);
    }
    std::map<std::string, std::string>::iterator    it = _headers.find("transfer-encoding");
    if (it != _headers.end() && toLower(it->second).find("chunked") != std::string::npos)
    {
        _isChunked = true;
        _chunkSubState = 0;
        _state = PARSE_CHUNKED;
        return (true);
    }
    it = _headers.find("content-length");
    if (it != _headers.end())
    {
        char*   endPtr = NULL;
        long    len = std::strtol(it->second.c_str(), &endPtr, 10);
        if (it->second.empty() || *endPtr != '\0' || len < 0)
        {
            _state = PARSE_ERROR;
            return (true);
        }
        _contentLength = static_cast<size_t>(len);
        _hasContentLength = true;
        if (_contentLength == 0)
        {
            _state = PARSE_COMPLETE;
            return (true);
        }
        _state = PARSE_BODY;
        return (true);
    }
    _state = PARSE_COMPLETE;
    return (true);
}

bool    HttpRequest::parseBody()
{
    if (_buffer.size() < _contentLength)
        return (false);
    _body = _buffer.substr(0, _contentLength);
    _buffer.erase(0, _contentLength);
    _state = PARSE_COMPLETE;
    return (true);
}

bool    HttpRequest::parseChunked()
{
    if (_chunkSubState == 0)
    {
        size_t  pos = _buffer.find("\r\n");
        if (pos == std::string::npos)
            return (false);
        
        std::string line = _buffer.substr(0, pos);
        _buffer.erase(0, pos + 2);
        size_t  semi = line.find(';');
        std::string sizeStr = (semi == std::string::npos) ? line : line.substr(0, semi);
        sizeStr = trim(sizeStr);
        if (sizeStr.empty())
        {
            _state = PARSE_ERROR;
            return (true);
        }
        char*   endPtr = NULL;
        long    size = std::strtol(sizeStr.c_str(), &endPtr, 16);
        if (*endPtr != '\0' || size < 0)
        {
            _state = PARSE_ERROR;
            return (true);
        }
        _chunkBytesRemaining = static_cast<size_t>(size);
        _chunkSubState = (_chunkBytesRemaining == 0) ? 2 : 1;
        return (true);
    }
    else if (_chunkSubState == 1)
    {
        if (_buffer.size() < _chunkBytesRemaining + 2)
            return (false);
        _body.append(_buffer, 0, _chunkBytesRemaining);
        _buffer.erase(0, _chunkBytesRemaining);
        
        if (_buffer.compare(0, 2, "\r\n") != 0)
        {
            _state = PARSE_ERROR;
            return (true);
        }
        _buffer.erase(0, 2);
        _chunkSubState = 0;
        return (true);
    }
    else
    {
        size_t  pos = _buffer.find("\r\n");
        if (pos == std::string::npos)
            return (false);
        std::string line = _buffer.substr(0, pos);
        _buffer.erase(0, pos + 2);
        if (line.empty())
        {
            _state = PARSE_COMPLETE;
            return (true);
        }
        return (true);
    }
}

bool    HttpRequest::isComplete() const { return (_state == PARSE_COMPLETE); }
bool    HttpRequest::hasError() const { return (_state == PARSE_ERROR); }
Method  HttpRequest::method() const { return (_method); }
const std::string&  HttpRequest::path() const { return (_path); }
const std::string&  HttpRequest::query() const { return (_query); }
const std::string&  HttpRequest::body() const { return (_body); }

std::string HttpRequest::header(const std::string& name) const
{
    std::map<std::string, std::string>::const_iterator  it = _headers.find(toLower(name));
    if (it == _headers.end())
        return ("");
    return (it->second);
}

void    HttpRequest::reset()
{
    _method = METHOD_UNKNOWN;
    _target.clear();
    _path.clear();
    _query.clear();
    _headers.clear();
    _body.clear();
    _state = PARSE_REQUEST_LINE;
    _hasContentLength = false;
    _contentLength = 0;
    _isChunked = false;
    _chunkBytesRemaining = 0;
    _chunkSubState = 0;
}
