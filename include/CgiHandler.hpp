#ifndef CGIHANDLER_HPP
# define CGIHANDLER_HPP

# include <string>
# include <map>
# include <vector>
# include <ctime>
# include <sys/types.h>
# include "HttpRequest.hpp"
# include "HttpResponse.hpp"
# include "Route.hpp"

class CgiHandler {
private:
	std::string							_scriptPath;
	std::string							_cgiExecutable;
	HttpRequest&						_request;
	HttpResponse&						_response;
	std::map<std::string, std::string>	_env;
	std::string							_output;
	int									_exitStatus;
	pid_t								_pid;
	int									_inputFd;
	int									_outputFd;
	bool								_inputOpen;
	bool								_outputOpen;
	bool								_processDone;
	size_t								_inputWritten;
	std::string							_input;
	int									_serverPort;
	time_t								_startTime;

	// Helper methods
	void		setupEnvironment(const Route& route);
	void		parseOutput();
	char**		getEnvArray() const;
	void		freeEnvArray(char** env) const;

public:
	// Orthodox Canonical Form
	CgiHandler(HttpRequest& request, HttpResponse& response);
	CgiHandler(const CgiHandler& other);
	CgiHandler& operator=(const CgiHandler& other);
	~CgiHandler();

	// CGI execution
	bool		start(const std::string& scriptPath, const Route& route);
	ssize_t		writeInput();
	ssize_t		readOutput();
	void		closeInput();
	void		closeOutput();
	bool		isComplete();
	bool		isTimeout(time_t now, time_t timeout) const;
	void		killProcess();
	void		finish();
	void		setGatewayError(int statusCode);
	void		setEnvVariable(const std::string& key, const std::string& value);

	// Getters
	int					getInputFd() const;
	int					getOutputFd() const;
	bool				wantsInputWrite() const;
	bool				wantsOutputRead() const;

	// Setters
	void		setServerPort(int port);

	// Utility methods
	static bool	isCgiRequest(const std::string& path, const Route& route);
	static std::string	getCgiExecutablePath(const std::string& extension, const Route& route);
};

#endif
