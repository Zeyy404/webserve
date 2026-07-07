#ifndef CLIENT_HPP
# define CLIENT_HPP

# include <string>
# include <netinet/in.h>
# include "HttpRequest.hpp"
# include "HttpResponse.hpp"
# include "ServerConfig.hpp"
# include "CgiHandler.hpp"
# include "iostream"

// One accepted connection. Clients live by value in the server's
// std::map<int, Client>, so copies must be safe: operator= deliberately does
// NOT copy _cgi (the map entry keeps sole ownership of the CGI process), and
// the destructor does NOT close _fd — the socket is closed only through an
// explicit close() from Server::closeClientConnection.
class Client {
private:
	int					_fd;
	struct sockaddr_in	_address;
	HttpRequest			_request;
	HttpResponse		_response;
	std::string			_readBuffer;
	std::string			_writeBuffer;
	bool				_keepAlive;
	time_t				_lastActivity;
	ServerConfig*		_serverConfig;
	CgiHandler*			_cgi;

	// Helper methods
	void		updateLastActivity();

public:
	// Orthodox Canonical Form
	Client();
	Client(int fd, const struct sockaddr_in& address);
	Client(const Client& other);
	Client& operator=(const Client& other);
	~Client();

	// Client operations.
	// read()/write() perform exactly ONE recv/send and must only be called
	// after select() reported the socket ready. Return value contract:
	//   > 0  bytes transferred;  0  peer closed (read) / nothing to send
	//   (write);  < 0  fatal — the caller must close the connection.
	// errno is never inspected, as the subject requires.
	ssize_t		read();
	ssize_t		write();
	void		processRequest();
	void		prepareResponse();
	void		processCgiInput();
	void		processCgiOutput();
	void		finishCgi();
	void		failCgiTimeout();
	bool		isReadComplete() const;
	bool		isWriteComplete() const;
	void		close();

	// Getters
	int					getFd() const;
	HttpRequest&		getRequest();
	HttpResponse&		getResponse();
	bool				getKeepAlive() const;
	bool				hasActiveCgi() const;
	bool				isCgiComplete();
	bool				isCgiTimeout(time_t currentTime, time_t timeout) const;
	// CGI pipe fds for the select() sets; -1 once that direction is finished,
	// which tells the server to drop the fd from the master set.
	int					getCgiInputFd() const;
	int					getCgiOutputFd() const;

	// Setters
	void		setFd(int fd);
	void		setServerConfig(ServerConfig* config);
	void		setKeepAlive(bool keepAlive);

	// Buffer operations
	void		clearReadBuffer();
	void		clearWriteBuffer();

	// Utility methods
	bool		isTimeout(time_t currentTime, time_t timeout) const;
};

#endif
