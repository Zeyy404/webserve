#include "../include/Client.hpp"
#include "../include/RequestHandler.hpp"
#include "../include/SessionMiddleware.hpp"
#include "../include/Logger.hpp"
#include <unistd.h>
#include <ctime>
#include <sstream>
#include <arpa/inet.h>

namespace {
	static std::string ipToString(const struct sockaddr_in& address) {
		unsigned long ip = ntohl(address.sin_addr.s_addr);
		std::ostringstream oss;
		oss << ((ip >> 24) & 0xFF) << '.' << ((ip >> 16) & 0xFF) << '.'
			<< ((ip >> 8) & 0xFF) << '.' << (ip & 0xFF);
		return oss.str();
	}
}


// Orthodox Canonical Form
Client::Client() : _fd(-1), _writeOffset(0), _bodyOffset(0), _continueSent(false), _keepAlive(true), _lastActivity(std::time(NULL)), _serverConfig(NULL), _cgi(NULL), _bodyFd(-1), _bodyFileSize(0), _bodyFileSent(0) {
}

Client::Client(int fd, const struct sockaddr_in& address) : _fd(fd), _address(address), _writeOffset(0), _bodyOffset(0), _continueSent(false), _keepAlive(true), _lastActivity(std::time(NULL)), _serverConfig(NULL), _cgi(NULL), _bodyFd(-1), _bodyFileSize(0), _bodyFileSent(0) {
}

Client::Client(const Client& other) {
	*this = other;
}

// Copies connection state but NOT owned resources: _cgi, _bodyFd and the
// body-file counters are reset so a copy never double-frees the CGI process or
// closes another Client's temp-file fd (Clients are stored by value in a map).
Client& Client::operator=(const Client& other) {
	if (this != &other) {
		_fd = other._fd;
		_address = other._address;
		_request = other._request;
		_response = other._response;
		_readBuffer = other._readBuffer;
		_writeBuffer = other._writeBuffer;
		_writeOffset = other._writeOffset;
		_bodyOffset = other._bodyOffset;
		_continueSent = other._continueSent;
		_keepAlive = other._keepAlive;
		_lastActivity = other._lastActivity;
		_serverConfig = other._serverConfig;
		_cgi = NULL;
		_bodyFd = -1;
		_bodyFileSize = 0;
		_bodyFileSent = 0;
	}
	return *this;
}

// Kills any running CGI child and closes the spilled-body temp file. Does NOT
// close _fd — the socket is owned/closed explicitly via close().
Client::~Client() {
	if (_cgi != NULL) {
		_cgi->killProcess();
		delete _cgi;
		_cgi = NULL;
	}
	closeBodyFile();
}

// Client operations
// One recv into the request parser. Emits an immediate "100 Continue" the first
// time the client announces Expect: 100-continue so upload bodies flow. Returns
// bytes read, 0 on peer close, -1 on error (see header for the contract).
ssize_t Client::read() {
	char buffer[8192];
	ssize_t bytes = ::recv(_fd, buffer, sizeof(buffer), 0);
	if (bytes > 0) {
		_request.appendData(std::string(buffer, static_cast<size_t>(bytes)));
		if (!_continueSent && _request.expects100Continue()) {
			const char* cont = "HTTP/1.1 100 Continue\r\n\r\n";
			::send(_fd, cont, 25, MSG_NOSIGNAL);
			_continueSent = true;
		}
		updateLastActivity();
		return bytes;
	}
	if (bytes == 0)
		return 0;
	return -1;
}

// Sends the response in one send() per call, in order: head buffer, then body.
// The body comes from a temp file (large CGI output) or the in-memory response
// buffer, never both. Returns bytes sent, 0 when nothing is left to send, -1 on
// error.
ssize_t Client::write() {
	// Headers first (small). One send per call, as select() requires.
	if (_writeOffset < _writeBuffer.size()) {
		ssize_t bytes = ::send(_fd, _writeBuffer.c_str() + _writeOffset, _writeBuffer.size() - _writeOffset, MSG_NOSIGNAL);
		if (bytes > 0) {
			_writeOffset += static_cast<size_t>(bytes);
			updateLastActivity();
			return bytes;
		}
		return -1;
	}
	if (_response.isBodySuppressed())
		return 0;
	// A large CGI body lives in a temp file, streamed in bounded chunks so the
	// server never holds the whole 100MB entity in memory.
	if (_bodyFd >= 0) {
		if (_bodyFileSent >= _bodyFileSize)
			return 0;
		char buffer[65536];
		if (::lseek(_bodyFd, static_cast<off_t>(_bodyFileSent), SEEK_SET) < 0)
			return -1;
		ssize_t n = ::read(_bodyFd, buffer, sizeof(buffer));
		if (n <= 0)
			return -1;
		ssize_t bytes = ::send(_fd, buffer, static_cast<size_t>(n), MSG_NOSIGNAL);
		if (bytes > 0) {
			_bodyFileSent += static_cast<size_t>(bytes);
			updateLastActivity();
			return bytes;
		}
		return -1;
	}
	// Otherwise the body is in the response buffer, streamed straight from there
	// so it is never duplicated into _writeBuffer.
	const std::string& body = _response.getBody();
	if (_bodyOffset < body.size()) {
		ssize_t bytes = ::send(_fd, body.c_str() + _bodyOffset, body.size() - _bodyOffset, MSG_NOSIGNAL);
		if (bytes > 0) {
			_bodyOffset += static_cast<size_t>(bytes);
			updateLastActivity();
			return bytes;
		}
		return -1;
	}
	return 0;
}

// Turns a fully-read request into a response: resets prior state, runs session
// middleware, and either starts a CGI child (kept in _cgi, returns early so the
// event loop can pump the pipes) or handles the request synchronously. Also
// fixes the Connection header from the negotiated keep-alive decision.
void Client::processRequest() {
	if (_serverConfig == NULL)
		return;

	_response.clear();
	closeBodyFile();
	if (_cgi != NULL) {
		_cgi->killProcess();
		delete _cgi;
		_cgi = NULL;
	}

	if (!_request.isValid()) {
		_response.buildErrorResponse(_request.getErrorCode(), "");
		setKeepAlive(false);
		_response.addHeader("Connection", "close");
		return;
	}

	SessionMiddleware sessionMiddleware;
	sessionMiddleware.processRequest(_request);

	RequestHandler handler(_request, _response, *_serverConfig);
	CgiHandler* cgi = new CgiHandler(_request, _response);
	cgi->setRemoteAddr(ipToString(_address));
	bool handled = false;
	if (handler.startCgiIfNeeded(*cgi, handled)) {
		_cgi = cgi;
		updateLastActivity();
		return;
	}
	delete cgi;

	if (!handled)
		handler.handle();

	sessionMiddleware.processResponse(_request, _response);
	setKeepAlive(_request.keepAlive());
	if (_keepAlive)
		_response.addHeader("Connection", "keep-alive");
	else
		_response.addHeader("Connection", "close");
}

// Serializes the response head into _writeBuffer, resets the send offsets, and
// emits the access-log line. Suppresses the body for HEAD requests.
void Client::prepareResponse() {
	if (_request.getMethod() == "HEAD")
		_response.setSuppressBody(true);
	_writeBuffer = _response.buildHead();
	_writeOffset = 0;
	_bodyOffset = 0;

	std::ostringstream oss;
	oss << ipToString(_address) << " \""
		<< (_request.getMethod().empty() ? "-" : _request.getMethod()) << " "
		<< (_request.getUri().empty() ? "-" : _request.getUri()) << "\" "
		<< _response.getStatusCode() << " "
		<< (_response.hasExternalBody() ? _response.getExternalBodyLength() : _response.getBody().size());
	Logger::getInstance()->info(oss.str());
}

void Client::processCgiInput() {
	if (_cgi == NULL)
		return;
	_cgi->writeInput();
	updateLastActivity();
}

void Client::processCgiOutput() {
	if (_cgi == NULL)
		return;
	_cgi->readOutput();
	updateLastActivity();
}

// Finalizes a completed CGI: parses its output into the response and, if it
// spilled a large body, takes ownership of the temp file fd so write() can
// stream it after the handler is destroyed. Then runs response middleware and
// sets the Connection header.
void Client::finishCgi() {
	if (_cgi == NULL)
		return;
	_cgi->finish();
	// Take ownership of the spilled-body temp file (if any) so it can be
	// streamed to the client after the CGI handler is gone.
	closeBodyFile();
	if (_response.hasExternalBody() && _cgi->hasBodyFile())
		_bodyFd = _cgi->releaseBodyFile(_bodyFileSize);
	delete _cgi;
	_cgi = NULL;
	SessionMiddleware sessionMiddleware;
	sessionMiddleware.processResponse(_request, _response);
	setKeepAlive(_request.keepAlive());
	if (_keepAlive)
		_response.addHeader("Connection", "keep-alive");
	else
		_response.addHeader("Connection", "close");
}

// Aborts a CGI that exceeded its deadline: kills the child, builds a 504
// response, and forces the connection closed.
void Client::failCgiTimeout() {
	if (_cgi == NULL)
		return;
	_cgi->killProcess();
	_cgi->setGatewayError(504);
	delete _cgi;
	_cgi = NULL;
	setKeepAlive(false);
	_response.addHeader("Connection", "close");
}

bool Client::isReadComplete() const {
	return _request.isComplete();
}

// True once head and body are fully sent, accounting for a suppressed body and
// for the temp-file vs in-memory body sources.
bool Client::isWriteComplete() const {
	if (_writeOffset < _writeBuffer.size())
		return false;
	if (_response.isBodySuppressed())
		return true;
	if (_bodyFd >= 0)
		return _bodyFileSent >= _bodyFileSize;
	return _bodyOffset >= _response.getBody().size();
}

// Explicit teardown: kills the CGI, closes the temp file, and closes the
// socket fd (which the destructor deliberately leaves alone).
void Client::close() {
	if (_cgi != NULL) {
		_cgi->killProcess();
		delete _cgi;
		_cgi = NULL;
	}
	closeBodyFile();
	if (_fd != -1) {
		::close(_fd);
		_fd = -1;
	}
}

// Private helper methods
void Client::updateLastActivity() {
	_lastActivity = std::time(NULL);
}

// Closes the streamed-body temp file (if open) and resets its counters.
void Client::closeBodyFile() {
	if (_bodyFd >= 0)
		::close(_bodyFd);
	_bodyFd = -1;
	_bodyFileSize = 0;
	_bodyFileSent = 0;
}

// Getters
int Client::getFd() const {
	return _fd;
}

HttpRequest& Client::getRequest() {
	return _request;
}

HttpResponse& Client::getResponse() {
	return _response;
}

bool Client::getKeepAlive() const {
	return _keepAlive;
}

bool Client::hasActiveCgi() const {
	return _cgi != NULL;
}

bool Client::isCgiComplete() {
	return _cgi != NULL && _cgi->isComplete();
}

bool Client::isCgiTimeout(time_t currentTime, time_t timeout) const {
	return _cgi != NULL && _cgi->isTimeout(currentTime, timeout);
}

int Client::getCgiInputFd() const {
	if (_cgi == NULL || !_cgi->wantsInputWrite())
		return -1;
	return _cgi->getInputFd();
}

int Client::getCgiOutputFd() const {
	if (_cgi == NULL || !_cgi->wantsOutputRead())
		return -1;
	return _cgi->getOutputFd();
}

// Setters
void Client::setFd(int fd) {
	_fd = fd;
}

// Also propagates the configured client_max_body_size to the request parser so
// oversized uploads are rejected during reading.
void Client::setServerConfig(ServerConfig* config) {
	_serverConfig = config;
	if (config != NULL)
		_request.setMaxBodySize(config->getClientMaxBodySize());
}

void Client::setKeepAlive(bool keepAlive) {
	_keepAlive = keepAlive;
}

// Buffer operations
void Client::clearReadBuffer() {
	_readBuffer.clear();
	_continueSent = false;
}

void Client::clearWriteBuffer() {
	_writeBuffer.clear();
	_writeOffset = 0;
	_bodyOffset = 0;
	closeBodyFile();
}

// Utility methods
bool Client::isTimeout(time_t currentTime, time_t timeout) const {
	return (currentTime - _lastActivity) > timeout;
}
