#include "../include/HttpRequest.hpp"
#include "../include/HttpUtils.hpp"

#include <cstdlib>
#include <sstream>
#include <algorithm>
#include <cctype>

using HttpUtils::trimWhitespace;

namespace {
	static std::string toLowerCase(const std::string& value) {
		std::string out = value;
		for (size_t i = 0; i < out.size(); ++i)
			out[i] = static_cast<char>(std::tolower(out[i]));
		return out;
	}

	static bool parseHexSize(const std::string& line, size_t& size) {
		std::string clean = trimWhitespace(line);
		size_t semicolon = clean.find(';');
		if (semicolon != std::string::npos)
			clean = clean.substr(0, semicolon);
		if (clean.empty())
			return false;
		for (size_t i = 0; i < clean.size(); ++i) {
			if (!std::isxdigit(static_cast<unsigned char>(clean[i])))
				return false;
		}
		size = static_cast<size_t>(std::strtoul(clean.c_str(), NULL, 16));
		return true;
	}

	static bool isToken(const std::string& value) {
		if (value.empty())
			return false;
		for (size_t i = 0; i < value.size(); ++i) {
			unsigned char c = static_cast<unsigned char>(value[i]);
			if (!std::isalpha(c))
				return false;
		}
		return true;
	}

	static bool isDigits(const std::string& value) {
		if (value.empty())
			return false;
		for (size_t i = 0; i < value.size(); ++i) {
			if (!std::isdigit(static_cast<unsigned char>(value[i])))
				return false;
		}
		return true;
	}

	const size_t MAX_REQUEST_LINE = 8192;
	const size_t MAX_HEADER_SECTION = 32768;
}

// Orthodox Canonical Form
HttpRequest::HttpRequest() : _httpVersion("HTTP/1.1"), _isComplete(false), _isChunked(false), _isValid(true), _errorCode(0), _contentLength(0), _maxBodySize(0), _headersParsed(false), _chunkPos(0), _session(NULL), _sessionId("") {}


HttpRequest::HttpRequest(const HttpRequest& other) {
	*this = other;
}

HttpRequest& HttpRequest::operator=(const HttpRequest& other) {
	if (this != &other) {
		_method = other._method;
		_uri = other._uri;
		_httpVersion = other._httpVersion;
		_headers = other._headers;
		_body = other._body;
		_queryString = other._queryString;
		_isComplete = other._isComplete;
		_isChunked = other._isChunked;
		_isValid = other._isValid;
		_errorCode = other._errorCode;
		_contentLength = other._contentLength;
		_maxBodySize = other._maxBodySize;
		_rawRequest = other._rawRequest;
		_headersParsed = other._headersParsed;
		_chunkPos = other._chunkPos;
		_session = other._session;
		_sessionId = other._sessionId;
	}
	return *this;
}

HttpRequest::~HttpRequest() {
}

// Parsing
// One-shot parse of a fully-buffered request (used mainly by tests). Locates the
// header/body separator, validates the header section, then handles the body per
// Content-Length or chunked. Returns false only on a hard parse error.
bool HttpRequest::parse(const std::string& rawRequest) {
	clear();
	_rawRequest = rawRequest;

	size_t headerEnd = rawRequest.find("\r\n\r\n");
	size_t sepLength = 4;
	if (headerEnd == std::string::npos) {
		headerEnd = rawRequest.find("\n\n");
		sepLength = 2;
	}
	if (headerEnd == std::string::npos) {
		if (rawRequest.find('\n') == std::string::npos && rawRequest.size() > MAX_REQUEST_LINE)
			return setError(414);
		if (rawRequest.size() > MAX_HEADER_SECTION)
			return setError(431);
		return true;
	}
	if (headerEnd > MAX_HEADER_SECTION)
		return setError(431);

	std::string bodySection = rawRequest.substr(headerEnd + sepLength);
	if (!parseHeaderSection(rawRequest.substr(0, headerEnd)))
		return false;
	_headersParsed = true;

	// Reject up front when the declared Content-Length exceeds the limit (chunked
	// bodies are checked incrementally in parseChunkedBody as they grow).
	if (_maxBodySize > 0 && !_isChunked && _contentLength > _maxBodySize)
		return setError(413);

	if (_isChunked) {
		parseChunkedBody(bodySection);
		return true;
	}

	_body = bodySection;
	finalizeBodyIfComplete();
	return true;
}

// Splits the request line from the header lines, parses both, and enforces the
// HTTP/1.1 mandatory Host header. Returns false (and marks complete) on any
// malformed line so the caller stops and emits the error code.
bool HttpRequest::parseHeaderSection(const std::string& headerSection) {
	size_t lineEnd = headerSection.find("\r\n");
	size_t lineSep = 2;
	if (lineEnd == std::string::npos) {
		lineEnd = headerSection.find('\n');
		lineSep = 1;
	}
	std::string requestLine;
	std::string headers;
	if (lineEnd == std::string::npos) {
		requestLine = headerSection;
		headers = "";
	} else {
		requestLine = headerSection.substr(0, lineEnd);
		headers = headerSection.substr(lineEnd + lineSep);
	}

	if (requestLine.size() > MAX_REQUEST_LINE)
		return setError(414);

	parseRequestLine(requestLine);
	if (!_isValid) {
		_isComplete = true;
		return false;
	}
	parseHeaders(headers);
	if (!_isValid) {
		_isComplete = true;
		return false;
	}
	if (_httpVersion == "HTTP/1.1" && (!hasHeader("host") || getHeader("host").empty()))
		return setError(400);
	return true;
}

// Marks the request complete once the buffered body reaches Content-Length,
// trimming any bytes that belong to the next pipelined request.
void HttpRequest::finalizeBodyIfComplete() {
	if (_contentLength == 0) {
		_isComplete = true;
	} else if (_body.size() >= _contentLength) {
		if (_body.size() > _contentLength)
			_body.resize(_contentLength);
		_isComplete = true;
	}
}

// Incremental entry point: called once per socket read. Until the header
// terminator arrives it accumulates into _rawRequest and re-scans for the
// separator; once headers are parsed it switches to appending body bytes
// (chunked via parseChunkedBody, otherwise raw until Content-Length is met).
void HttpRequest::appendData(const std::string& data) {
	if (_isComplete)
		return;

	if (!_headersParsed) {
		_rawRequest.append(data);

		size_t sepLength = 4;
		size_t headerEnd = _rawRequest.find("\r\n\r\n");
		if (headerEnd == std::string::npos) {
			headerEnd = _rawRequest.find("\n\n");
			sepLength = 2;
		}
		if (headerEnd == std::string::npos) {
			if (_rawRequest.find('\n') == std::string::npos && _rawRequest.size() > MAX_REQUEST_LINE)
				setError(414);
			else if (_rawRequest.size() > MAX_HEADER_SECTION)
				setError(431);
			return;
		}
		if (headerEnd > MAX_HEADER_SECTION) {
			setError(431);
			return;
		}

		std::string leftover = _rawRequest.substr(headerEnd + sepLength);
		std::string headerSection = _rawRequest.substr(0, headerEnd);
		_rawRequest.clear();
		_headersParsed = true;

		if (!parseHeaderSection(headerSection))
			return;
		if (_maxBodySize > 0 && !_isChunked && _contentLength > _maxBodySize) {
			setError(413);
			return;
		}

		if (_isChunked) {
			_rawRequest = leftover;
			parseChunkedBody(_rawRequest);
		} else {
			_body = leftover;
			finalizeBodyIfComplete();
		}
		return;
	}

	if (_isChunked) {
		_rawRequest.append(data);
		parseChunkedBody(_rawRequest);
	} else {
		_body.append(data);
		finalizeBodyIfComplete();
	}
}

bool HttpRequest::setError(int code) {
	_isValid = false;
	_errorCode = code;
	_isComplete = true;
	return false;
}

void HttpRequest::setMaxBodySize(size_t maxBodySize) {
	_maxBodySize = maxBodySize;
}

bool HttpRequest::isComplete() const {
	return _isComplete;
}

bool HttpRequest::isValid() const {
	return _isValid;
}

bool HttpRequest::expects100Continue() const {
	if (!_headersParsed || _isComplete || !_isValid)
		return false;
	if (!hasHeader("expect"))
		return false;
	return toLowerCase(getHeader("expect")) == "100-continue";
}

int HttpRequest::getErrorCode() const {
	return _errorCode;
}

void HttpRequest::clearBody() {
	std::string().swap(_body);
}

void HttpRequest::clear() {
	_method.clear();
	_uri.clear();
	_httpVersion = "HTTP/1.1";
	_headers.clear();
	_body.clear();
	_queryString.clear();
	_isComplete = false;
	_isChunked = false;
	_isValid = true;
	_errorCode = 0;
	_contentLength = 0;
	_rawRequest.clear();
	_headersParsed = false;
	_chunkPos = 0;
	_session = NULL;
	_sessionId.clear();
}

// Private parsing helpers
// Validates and splits "METHOD URI VERSION": rejects extra tokens, non-token
// methods, and non-absolute URIs (400); unknown HTTP/x versions get 505, other
// malformed version strings 400.
void HttpRequest::parseRequestLine(const std::string& line) {
	std::istringstream iss(line);
	std::string extra;
	if (!(iss >> _method >> _uri >> _httpVersion) || (iss >> extra)) {
		_isValid = false;
		_errorCode = 400;
		return;
	}
	if (!isToken(_method) || _uri.empty() || _uri[0] != '/') {
		_isValid = false;
		_errorCode = 400;
		return;
	}
	if (_httpVersion != "HTTP/1.0" && _httpVersion != "HTTP/1.1") {
		_isValid = false;
		_errorCode = (_httpVersion.compare(0, 5, "HTTP/") == 0) ? 505 : 400;
		return;
	}
	parseUri();
}

// Parses each "Key: Value" line into _headers, rejecting malformed lines,
// duplicate/whitespace-in-name headers, and embedded CR/LF (400). Enforces that
// Content-Length and Transfer-Encoding are mutually exclusive, then resolves the
// effective body framing (_contentLength or _isChunked).
void HttpRequest::parseHeaders(const std::string& headerSection) {
	std::istringstream iss(headerSection);
	std::string line;
	bool hasContentLength = false;
	bool hasTransferEncoding = false;

	while (std::getline(iss, line)) {
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);
		if (line.empty())
			continue;

		size_t colon = line.find(':');
		if (colon == std::string::npos || colon == 0) {
			_isValid = false;
			_errorCode = 400;
			continue;
		}

		std::string key = line.substr(0, colon);
		std::string value = line.substr(colon + 1);
		if (key.find_first_of(" \t") != std::string::npos) {
			_isValid = false;
			_errorCode = 400;
			continue;
		}
		while (!value.empty() && (value[0] == ' ' || value[0] == '\t'))
			value.erase(0, 1);
		std::string lowerKey = toLowerCase(key);

		if (lowerKey == "host" && _headers.find("host") != _headers.end()) {
			_isValid = false;
			_errorCode = 400;
			continue;
		}

		if (lowerKey == "content-length") {
			if (hasContentLength || hasTransferEncoding) {
				_isValid = false;
				_errorCode = 400;
				continue;
			}
			hasContentLength = true;
		}

		if (lowerKey == "transfer-encoding") {
			if (hasContentLength) {
				_isValid = false;
				_errorCode = 400;
				continue;
			}
			hasTransferEncoding = true;
		}

		for (size_t i = 0; i < value.size(); ++i) {
			if (value[i] == '\r' || value[i] == '\n') {
				_isValid = false;
				_errorCode = 400;
				break;
			}
		}
		if (!_isValid)
			continue;

		// Store under the lower-cased name so lookups are case-insensitive.
		_headers[lowerKey] = value;
	}

	if (!_isValid)
		return;
	if (hasHeader("content-length")) {
		std::string cl = getHeader("content-length");
		if (!isDigits(cl)) {
			_isValid = false;
			_errorCode = 400;
			return;
		}
		_contentLength = static_cast<size_t>(std::strtoul(cl.c_str(), NULL, 10));
		if (_contentLength > 0 && cl.find_first_not_of("0123456789") != std::string::npos) {
			_isValid = false;
			_errorCode = 400;
			return;
		}
	}
	if (hasHeader("transfer-encoding") && toLowerCase(getHeader("transfer-encoding")) == "chunked")
		_isChunked = true;
}

void HttpRequest::parseBody(const std::string& bodySection) {
	_body = bodySection;
}

void HttpRequest::parseUri() {
	size_t queryPos = _uri.find('?');
	if (queryPos == std::string::npos) {
		_queryString.clear();
		return;
	}
	_queryString = _uri.substr(queryPos + 1);
}

// Incremental chunked decoder: resumes from _chunkPos across calls, appending
// only fully-received chunks to _body so re-invocation on each packet stays
// O(total bytes) instead of re-decoding the whole accumulated buffer.
void HttpRequest::parseChunkedBody(const std::string& bodySection) {
	while (_chunkPos < bodySection.size()) {
		size_t lineEnd = bodySection.find("\r\n", _chunkPos);
		size_t sepLength = 2;
		if (lineEnd == std::string::npos) {
			lineEnd = bodySection.find('\n', _chunkPos);
			sepLength = 1;
		}
		if (lineEnd == std::string::npos)
			return;

		size_t chunkSize = 0;
		if (!parseHexSize(bodySection.substr(_chunkPos, lineEnd - _chunkPos), chunkSize)) {
			setError(400);
			return;
		}
		size_t dataStart = lineEnd + sepLength;

		if (chunkSize == 0) {
			// last-chunk: consume the optional trailer section, which is
			// terminated by a blank line. Only complete once that final CRLF
			// has arrived, otherwise leftover bytes corrupt the next request.
			size_t pos = dataStart;
			while (true) {
				size_t trailerEnd = bodySection.find("\r\n", pos);
				size_t trailerSep = 2;
				if (trailerEnd == std::string::npos) {
					trailerEnd = bodySection.find('\n', pos);
					trailerSep = 1;
				}
				if (trailerEnd == std::string::npos)
					return;
				if (trailerEnd == pos) {
					_chunkPos = pos + trailerSep;
					_contentLength = _body.size();
					_isComplete = true;
					return;
				}
				pos = trailerEnd + trailerSep;
			}
		}

		if (bodySection.size() < dataStart + chunkSize)
			return;

		size_t afterData = dataStart + chunkSize;
		size_t sepAdvance;
		if (afterData >= bodySection.size())
			return;
		if (bodySection[afterData] == '\r') {
			if (afterData + 1 >= bodySection.size())
				return;
			if (bodySection[afterData + 1] != '\n') {
				setError(400);
				return;
			}
			sepAdvance = 2;
		} else if (bodySection[afterData] == '\n') {
			sepAdvance = 1;
		} else {
			setError(400);
			return;
		}

		_body.append(bodySection, dataStart, chunkSize);
		if (_maxBodySize > 0 && _body.size() > _maxBodySize) {
			setError(413);
			return;
		}
		_chunkPos = afterData + sepAdvance;
	}
}

// Getters
const std::string& HttpRequest::getMethod() const {
	return _method;
}

const std::string& HttpRequest::getUri() const {
	return _uri;
}

const std::string& HttpRequest::getHttpVersion() const {
	return _httpVersion;
}

const std::map<std::string, std::string>& HttpRequest::getHeaders() const {
	return _headers;
}

std::string HttpRequest::getHeader(const std::string& key) const {
	std::map<std::string, std::string>::const_iterator it = _headers.find(toLowerCase(key));
	if (it == _headers.end())
		return "";
	return it->second;
}

const std::string& HttpRequest::getBody() const {
	return _body;
}

const std::string& HttpRequest::getQueryString() const {
	return _queryString;
}

Session* HttpRequest::getSession() const {
	return _session;
}

const std::string& HttpRequest::getSessionId() const {
	return _sessionId;
}

//Setters
void HttpRequest::setSession(Session* session) {
	_session = session;
}

void HttpRequest::setSessionId(const std::string& id) {
	_sessionId = id;
}

// Utility methods
bool HttpRequest::hasHeader(const std::string& key) const {
	return _headers.find(toLowerCase(key)) != _headers.end();
}

// Connection persistence per HTTP version: 1.1 defaults to keep-alive unless
// "Connection: close", whereas 1.0 defaults to close unless "keep-alive".
bool HttpRequest::keepAlive() const {
	std::string connection = toLowerCase(getHeader("connection"));
	if (_httpVersion == "HTTP/1.1")
		return connection != "close";
	return connection == "keep-alive";
}

std::string HttpRequest::getPath() const {
	size_t queryPos = _uri.find('?');
	if (queryPos == std::string::npos)
		return _uri.empty() ? "/" : _uri;
	return _uri.substr(0, queryPos);
}
