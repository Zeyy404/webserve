#ifndef SOCKET_HPP
# define SOCKET_HPP

# include <string>
# include <netinet/in.h>

// Thin RAII-ish wrapper around a single listening TCP socket fd. Handles the
// create/bind/listen sequence with SO_REUSEADDR and non-blocking mode set on
// creation. Copyable by fd value (no ownership transfer on copy), so the caller
// must manage which instance actually closes the fd.
class Socket {
private:
	int					_fd;
	int					_port;
	std::string			_host;
	struct sockaddr_in	_address;
	bool				_isListening;

	// Helper methods
	void		setNonBlocking();
	void		setSocketOptions();

public:
	// Orthodox Canonical Form
	Socket();
	Socket(const std::string& host, int port);
	Socket(const Socket& other);
	Socket& operator=(const Socket& other);
	~Socket();

	// Socket operations
	bool		create();
	bool		bind();
	bool		listen(int backlog);
	void		close();

	// Getters
	int					getFd() const;
	int					getPort() const;
	const std::string&	getHost() const;

	// Setters
	void		setFd(int fd);
};

#endif
