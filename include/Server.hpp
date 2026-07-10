#ifndef SERVER_HPP
# define SERVER_HPP

# include <string>
# include <vector>
# include <map>
# include "ServerConfig.hpp"
# include "Client.hpp"
# include "Socket.hpp"

// Owns the select()-based event loop: listening sockets, connected clients,
// and their CGI pipes. Multiplexes all I/O over the master read/write fd_sets,
// dispatches accept/read/write and CGI phases each tick, and enforces client
// and CGI timeouts. A given host:port is backed by a single shared listen socket.
class Server {
private:
	std::vector<ServerConfig>		_serverConfigs;
	std::vector<Socket>				_listenSockets;
	std::map<int, Client>			_clients;
	std::map<int, ServerConfig*>	_listenConfig;
	int								_maxFd;
	bool							_isRunning;
	fd_set							_readFds;
	fd_set							_writeFds;
	fd_set							_masterReadFds;
	fd_set							_masterWriteFds;

	// Private helper methods
	void		setupListenSockets();
	static bool	hasServerNameConflict(const ServerConfig& a, const ServerConfig& b);
	void		acceptNewConnection(int listenFd);
	void		handleClientRead(int clientFd);
	void		handleClientWrite(int clientFd);
	void		registerClientCgi(int clientFd);
	void		unregisterClientCgi(Client& client);
	void		handleCgiInput(int clientFd);
	void		handleCgiOutput(int clientFd);
	void		finishClientCgi(int clientFd);
	void		closeClientConnection(int clientFd);
	void		updateMaxFd();

public:
	// Orthodox Canonical Form
	Server();
	Server(const Server& other);
	Server& operator=(const Server& other);
	~Server();

	// Configuration
	void		addServerConfig(const ServerConfig& config);

	// Main server operations
	void		init();
	void		run();
	void		stop();

	// Socket management
	void		setupSockets();
	void		closeAllSockets();

	// Getters
	const std::vector<ServerConfig>&	getServerConfigs() const;
};

#endif
