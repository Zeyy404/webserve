#include "../include/Server.hpp"
#include "../include/Logger.hpp"
#include "../include/ConfigParser.hpp"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>

// Orthodox Canonical Form
Server::Server() : _maxFd(0), _isRunning(false) {
	FD_ZERO(&_masterReadFds);
	FD_ZERO(&_masterWriteFds);
}

Server::Server(const Server& other) {
	*this = other;
}

Server& Server::operator=(const Server& other) {
	if (this != &other) {
		_serverConfigs = other._serverConfigs;
		_listenSockets = other._listenSockets;
		_clients = other._clients;
		_listenConfig = other._listenConfig;
		_maxFd = other._maxFd;
		_isRunning = other._isRunning;
		_readFds = other._readFds;
		_writeFds = other._writeFds;
		_masterReadFds = other._masterReadFds;
		_masterWriteFds = other._masterWriteFds;
	}
	return *this;
}

Server::~Server() {
	closeAllSockets();
}

// Configuration
void Server::addServerConfig(const ServerConfig& config) {
	_serverConfigs.push_back(config);
}

// Main server operations
// Validates that config was loaded and builds the listening sockets. Throws if empty.
void Server::init() {
	if (_serverConfigs.empty())
		throw std::runtime_error("No server configuration loaded");
	setupSockets();
}

// The select() event loop. Each tick: copy master fd_sets, select with a 1s
// tick, accept new connections, then per client enforce CGI/idle timeouts and
// drive the read -> (CGI in/out) -> write phases. Runs until stop() is called.
void Server::run() {
	Logger::getInstance()->info("Entering main event loop");
	_isRunning = true;

	while (_isRunning) {
		_readFds = _masterReadFds;
		_writeFds = _masterWriteFds;

		struct timeval timeout;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		int ready = ::select(_maxFd + 1, &_readFds, &_writeFds, NULL, &timeout);
		if (ready < 0) {
			if (errno == EINTR)  // interrupted by a signal; just re-run the loop
				continue;
			throw std::runtime_error(std::string("select failed: ") + std::strerror(errno));
		}

		for (size_t i = 0; i < _listenSockets.size(); ++i) {
			int listenFd = _listenSockets[i].getFd();
			if (listenFd >= 0 && FD_ISSET(listenFd, &_readFds))
				acceptNewConnection(listenFd);
		}

		// Snapshot fds first: handlers below may erase clients mid-iteration.
		std::vector<int> clientFds;
		for (std::map<int, Client>::const_iterator it = _clients.begin(); it != _clients.end(); ++it)
			clientFds.push_back(it->first);

		time_t now = std::time(NULL);
		for (size_t i = 0; i < clientFds.size(); ++i) {
			int fd = clientFds[i];
			std::map<int, Client>::iterator it = _clients.find(fd);
			if (it == _clients.end())
				continue;

			// CGI stalled past 5s: unhook its pipes, turn it into a 504-style response, and go write.
			if (it->second.hasActiveCgi() && it->second.isCgiTimeout(now, 5)) {
				unregisterClientCgi(it->second);
				it->second.failCgiTimeout();
				it->second.prepareResponse();
				FD_SET(fd, &_masterWriteFds);
				updateMaxFd();
				continue;
			}

			// Idle client past 30s with no active CGI: drop the connection.
			if (it->second.isTimeout(now, 30)) {
				closeClientConnection(fd);
				continue;
			}

			if (!it->second.hasActiveCgi() && FD_ISSET(fd, &_readFds))
				handleClientRead(fd);

			if (_clients.find(fd) == _clients.end())  // read may have closed the client
				continue;

			it = _clients.find(fd);  // re-fetch: the read handler could have invalidated the iterator
			if (it->second.hasActiveCgi()) {
				int cgiInputFd = it->second.getCgiInputFd();
				int cgiOutputFd = it->second.getCgiOutputFd();
				if (cgiInputFd >= 0 && FD_ISSET(cgiInputFd, &_writeFds))
					handleCgiInput(fd);
				if (_clients.find(fd) != _clients.end() && cgiOutputFd >= 0 && FD_ISSET(cgiOutputFd, &_readFds))
					handleCgiOutput(fd);
				if (_clients.find(fd) != _clients.end() && _clients[fd].hasActiveCgi() && _clients[fd].isCgiComplete())
					finishClientCgi(fd);
			}

			if (_clients.find(fd) != _clients.end() && !_clients[fd].hasActiveCgi() && FD_ISSET(fd, &_writeFds))
				handleClientWrite(fd);
		}
	}
}

// Signals the event loop to exit and tears down all sockets/clients.
void Server::stop() {
	_isRunning = false;
	closeAllSockets();
}

// Socket management
void Server::setupSockets() {
	setupListenSockets();
	updateMaxFd();
}

// Closes every client and listen socket, clears fd_sets, and resets maxFd.
void Server::closeAllSockets() {
	for (std::map<int, Client>::iterator it = _clients.begin(); it != _clients.end(); ++it)
		it->second.close();
	_clients.clear();

	for (size_t i = 0; i < _listenSockets.size(); ++i)
		_listenSockets[i].close();
	_listenSockets.clear();
	_listenConfig.clear();

	FD_ZERO(&_masterReadFds);
	FD_ZERO(&_masterWriteFds);
	_maxFd = 0;
}

// Private helper methods
// Two configs on the same host:port collide if they share any server_name, or
// if either declares none (a nameless default can't be disambiguated).
bool Server::hasServerNameConflict(const ServerConfig& a, const ServerConfig& b) {
	const std::vector<std::string>& namesA = a.getServerNames();
	const std::vector<std::string>& namesB = b.getServerNames();
	if (namesA.empty() || namesB.empty())
		return true;
	for (size_t i = 0; i < namesA.size(); ++i)
		for (size_t j = 0; j < namesB.size(); ++j)
			if (namesA[i] == namesB[j])
				return true;
	return false;
}

// Creates one listen socket per unique host:port. Configs that reuse an existing
// host:port share its socket (mapped via _listenConfig) unless their server_names
// conflict, which throws. Throws on any create/bind/listen failure.
void Server::setupListenSockets() {
	_listenSockets.reserve(_serverConfigs.size());

	for (size_t i = 0; i < _serverConfigs.size(); ++i) {
		std::string host = _serverConfigs[i].getHost();
		int port = _serverConfigs[i].getPort();

		bool found = false;
		for (size_t j = 0; j < _listenSockets.size(); ++j) {
			if (_listenSockets[j].getHost() == host && _listenSockets[j].getPort() == port) {
				int fd = _listenSockets[j].getFd();
				if (hasServerNameConflict(*_listenConfig[fd], _serverConfigs[i])) {
					std::ostringstream oss;
					oss << "Duplicate listen " << host << ":" << port
						<< " with conflicting server_name";
					throw std::runtime_error(oss.str());
				}
				_listenConfig[fd] = &_serverConfigs[i];
				std::ostringstream oss;
				oss << "Sharing existing listen socket for " << host << ":" << port;
				Logger::getInstance()->info(oss.str());
				found = true;
				break;
			}
		}
		if (found)
			continue;

		Socket socket(host, port);
		if (!socket.create()) {
			std::ostringstream oss;
			oss << "Failed to create socket for " << host << ":" << port;
			throw std::runtime_error(oss.str());
		}
		if (!socket.bind()) {
			std::ostringstream oss;
			oss << "Failed to bind " << host << ":" << port
				<< " (" << std::strerror(errno) << ")";
			socket.close();
			throw std::runtime_error(oss.str());
		}
		if (!socket.listen(256)) {
			std::ostringstream oss;
			oss << "Failed to listen on " << host << ":" << port;
			socket.close();
			throw std::runtime_error(oss.str());
		}

		_listenSockets.push_back(socket);
		socket.setFd(-1);  // release the local's fd so its dtor won't close the now-shared fd
		int fd = _listenSockets.back().getFd();
		FD_SET(fd, &_masterReadFds);
		_listenConfig[fd] = &_serverConfigs[i];
		std::ostringstream oss;
		oss << "Listening on " << host << ":" << port;
		Logger::getInstance()->info(oss.str());
	}

	if (_listenSockets.empty())
		throw std::runtime_error("No listening sockets available");
}

// Accepts one pending connection on listenFd, sets it non-blocking, associates it
// with the listen socket's ServerConfig, registers it for reading, and tracks it.
// Silently returns on EAGAIN/EWOULDBLOCK (nothing actually pending).
void Server::acceptNewConnection(int listenFd) {
	struct sockaddr_in clientAddr;
	socklen_t clientLen = sizeof(clientAddr);
	int clientFd = ::accept(listenFd, reinterpret_cast<struct sockaddr*>(&clientAddr), &clientLen);
	if (clientFd < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			Logger::getInstance()->warning("Accept failed");
		return;
	}

	fcntl(clientFd, F_SETFL, O_NONBLOCK);

	Client client(clientFd, clientAddr);
	if (_listenConfig.find(listenFd) != _listenConfig.end())
		client.setServerConfig(_listenConfig[listenFd]);
	_clients[clientFd] = client;

	FD_SET(clientFd, &_masterReadFds);
	updateMaxFd();
}

// Reads from the client; closes on EOF/error. Once the full request is read,
// parses it and either registers CGI pipes or prepares the response and flips
// the fd from the read set to the write set.
void Server::handleClientRead(int clientFd) {
	std::map<int, Client>::iterator it = _clients.find(clientFd);
	if (it == _clients.end())
		return;

	ssize_t bytes = it->second.read();
	if (bytes <= 0) {
		if (bytes < 0)
			Logger::getInstance()->warning("Client read error");
		closeClientConnection(clientFd);
		return;
	}

	if (it->second.isReadComplete()) {
		it->second.processRequest();
		FD_CLR(clientFd, &_masterReadFds);
		if (it->second.hasActiveCgi()) {
			registerClientCgi(clientFd);
		} else {
			it->second.prepareResponse();
			FD_SET(clientFd, &_masterWriteFds);
		}
	}
}

// Writes queued response bytes. On completion: if keep-alive, reset request/response
// buffers and flip the fd back to the read set for the next request; otherwise close.
void Server::handleClientWrite(int clientFd) {
	std::map<int, Client>::iterator it = _clients.find(clientFd);
	if (it == _clients.end())
		return;

	ssize_t bytes = it->second.write();
	if (bytes < 0) {
		closeClientConnection(clientFd);
		return;
	}

	if (it->second.isWriteComplete()) {
		if (it->second.getKeepAlive()) {
			it->second.getRequest().clear();
			it->second.getResponse().clear();
			it->second.clearReadBuffer();
			it->second.clearWriteBuffer();
			FD_CLR(clientFd, &_masterWriteFds);
			FD_SET(clientFd, &_masterReadFds);
		} else {
			closeClientConnection(clientFd);
		}
	}
}

// Adds the client's CGI pipes to the master sets: input (write to child stdin)
// to the write set, output (read from child stdout) to the read set.
void Server::registerClientCgi(int clientFd) {
	std::map<int, Client>::iterator it = _clients.find(clientFd);
	if (it == _clients.end())
		return;
	int inputFd = it->second.getCgiInputFd();
	int outputFd = it->second.getCgiOutputFd();
	if (inputFd >= 0)
		FD_SET(inputFd, &_masterWriteFds);
	if (outputFd >= 0)
		FD_SET(outputFd, &_masterReadFds);
	updateMaxFd();
}

// Removes the client's CGI pipe fds from the master sets (mirror of registerClientCgi).
void Server::unregisterClientCgi(Client& client) {
	int inputFd = client.getCgiInputFd();
	int outputFd = client.getCgiOutputFd();
	if (inputFd >= 0)
		FD_CLR(inputFd, &_masterWriteFds);
	if (outputFd >= 0)
		FD_CLR(outputFd, &_masterReadFds);
}

// Feeds request body to the CGI child. If the input fd changed (e.g. closed once
// the body is fully sent), clear the stale fd from the write set.
void Server::handleCgiInput(int clientFd) {
	std::map<int, Client>::iterator it = _clients.find(clientFd);
	if (it == _clients.end())
		return;
	int oldFd = it->second.getCgiInputFd();
	it->second.processCgiInput();
	if (oldFd >= 0 && it->second.getCgiInputFd() != oldFd)
		FD_CLR(oldFd, &_masterWriteFds);
	updateMaxFd();
}

// Reads CGI child output. If the output fd changed (child stdout hit EOF/closed),
// clear the stale fd from the read set.
void Server::handleCgiOutput(int clientFd) {
	std::map<int, Client>::iterator it = _clients.find(clientFd);
	if (it == _clients.end())
		return;
	int oldFd = it->second.getCgiOutputFd();
	it->second.processCgiOutput();
	if (oldFd >= 0 && it->second.getCgiOutputFd() != oldFd)
		FD_CLR(oldFd, &_masterReadFds);
	updateMaxFd();
}

// CGI finished: unhook its pipes, reap/finalize the child, build the response
// from its output, and register the client fd for writing.
void Server::finishClientCgi(int clientFd) {
	std::map<int, Client>::iterator it = _clients.find(clientFd);
	if (it == _clients.end())
		return;
	unregisterClientCgi(it->second);
	it->second.finishCgi();
	it->second.prepareResponse();
	FD_SET(clientFd, &_masterWriteFds);
	updateMaxFd();
}

// Fully tears down a client: unregister its CGI pipes, close and erase it, and
// clear its fd from both master sets. Safe to call whether or not the fd is tracked.
void Server::closeClientConnection(int clientFd) {
	std::map<int, Client>::iterator it = _clients.find(clientFd);
	if (it != _clients.end()) {
		unregisterClientCgi(it->second);
		it->second.close();
		_clients.erase(it);
	}
	FD_CLR(clientFd, &_masterReadFds);
	FD_CLR(clientFd, &_masterWriteFds);
	updateMaxFd();
}

// Recomputes the highest fd across listen sockets, clients, and their CGI pipes
// so select()'s nfds argument (_maxFd + 1) stays correct after any fd add/remove.
void Server::updateMaxFd() {
	_maxFd = 0;
	for (size_t i = 0; i < _listenSockets.size(); ++i)
		if (_listenSockets[i].getFd() > _maxFd)
			_maxFd = _listenSockets[i].getFd();
	for (std::map<int, Client>::const_iterator it = _clients.begin(); it != _clients.end(); ++it)
	{
		if (it->first > _maxFd)
			_maxFd = it->first;
		int inputFd = it->second.getCgiInputFd();
		int outputFd = it->second.getCgiOutputFd();
		if (inputFd > _maxFd)
			_maxFd = inputFd;
		if (outputFd > _maxFd)
			_maxFd = outputFd;
	}
}

// Getters
const std::vector<ServerConfig>& Server::getServerConfigs() const {
	return _serverConfigs;
}
