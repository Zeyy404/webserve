/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Socket.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: myda-chi <myda-chi@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/24 20:01:36 by myda-chi          #+#    #+#             */
/*   Updated: 2026/06/24 20:01:37 by myda-chi         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../include/Socket.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstring>

// Orthodox Canonical Form
Socket::Socket() : _fd(-1), _port(0), _host("0.0.0.0"), _isListening(false) {
	std::memset(&_address, 0, sizeof(_address));
}

Socket::Socket(const std::string& host, int port) : _fd(-1), _port(port), _host(host), _isListening(false) {
	std::memset(&_address, 0, sizeof(_address));
}

Socket::Socket(const Socket& other) {
	*this = other;
}

Socket& Socket::operator=(const Socket& other) {
	if (this != &other) {
		_fd = other._fd;
		_port = other._port;
		_host = other._host;
		_address = other._address;
		_isListening = other._isListening;
	}
	return *this;
}

// Closes the owned fd if still open (fd set to -1 signals ownership was released).
Socket::~Socket() {
	if (_fd != -1)
		close();
}

// Socket operations
// Creates the TCP socket, applies SO_REUSEADDR + non-blocking, and fills in the
// bind address (INADDR_ANY when host is empty/0.0.0.0). Returns false on failure.
bool Socket::create() {
	_fd = ::socket(AF_INET, SOCK_STREAM, 0);
	if (_fd < 0)
		return false;

	setSocketOptions();
	setNonBlocking();

	std::memset(&_address, 0, sizeof(_address));
	_address.sin_family = AF_INET;
	_address.sin_port = htons(_port);
	if (_host.empty() || _host == "0.0.0.0")
		_address.sin_addr.s_addr = INADDR_ANY;
	else if (::inet_pton(AF_INET, _host.c_str(), &_address.sin_addr) != 1)
		return false;

	return true;
}

// Binds the fd to the address set up in create(); returns false if not created or bind fails.
bool Socket::bind() {
	if (_fd < 0)
		return false;
	return ::bind(_fd, reinterpret_cast<struct sockaddr*>(&_address), sizeof(_address)) == 0;
}

// Marks the socket as listening with the given backlog; returns false on failure.
bool Socket::listen(int backlog) {
	if (_fd < 0)
		return false;
	if (::listen(_fd, backlog) != 0)
		return false;
	_isListening = true;
	return true;
}

// Closes the fd and resets it to -1 so repeated/destructor closes are no-ops.
void Socket::close() {
	if (_fd != -1) {
		::close(_fd);
		_fd = -1;
	}
}

// Private helper methods
// Non-blocking so accept()/read()/write() never stall the single select() thread.
void Socket::setNonBlocking() {
	if (_fd < 0)
		return;
	fcntl(_fd, F_SETFL, O_NONBLOCK);
}

// SO_REUSEADDR so a restart can rebind the port while old connections are in TIME_WAIT.
void Socket::setSocketOptions() {
	if (_fd < 0)
		return;
	int opt = 1;
	setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}

// Getters
int Socket::getFd() const {
	return _fd;
}

int Socket::getPort() const {
	return _port;
}

const std::string& Socket::getHost() const {
	return _host;
}

// Setters
void Socket::setFd(int fd) {
	_fd = fd;
}
