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

// Runs one CGI invocation: forks/execs the interpreter, streams the request
// body to the child's stdin over a pipe, reads its stdout, and parses the CGI
// header block into the response. Large output bodies are spilled to an
// unlink'd temp file (later handed to the owning Client) so the whole entity is
// never buffered in memory. Owns the child process and pipe fds; copies must
// not duplicate the temp-file fd, so operator= resets _bodyFd to -1.
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
	int									_serverPort;
	time_t								_startTime;
	std::string							_scriptName;
	std::string							_pathInfo;
	std::string							_pathTranslated;
	std::string							_remoteAddr;
	// Large CGI bodies are spilled to a temp file so the server never holds the
	// whole entity in memory (a 100MB echo under heavy concurrency would OOM
	// otherwise). _output keeps only the header block once spilling starts.
	int									_bodyFd;
	size_t								_bodyBytes;
	bool								_headersLocated;
	size_t								_bodyStart;

	// Helper methods
	void		setupEnvironment(const Route& route);
	void		parseOutput();
	char**		getEnvArray() const;
	void		freeEnvArray(char** env) const;
	void		openBodyFile();
	void		closeBodyFile();

public:
	// Orthodox Canonical Form
	CgiHandler(HttpRequest& request, HttpResponse& response);
	CgiHandler(const CgiHandler& other);
	CgiHandler& operator=(const CgiHandler& other);
	~CgiHandler();

	// CGI execution.
	// start() forks and execs the interpreter; the child inherits its
	// environment at that moment, so setServerPort/setScriptName/setPathInfo/
	// setRemoteAddr must all be called BEFORE start().
	bool		start(const std::string& scriptPath, const Route& route);
	// One write/read per call; only call after select() reported the pipe fd
	// ready. A failed write is retried on the next event (stdin is closed
	// early only if the child already exited); stdin is closed after the last
	// body byte so the child sees EOF, and output is read until EOF.
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
	// Hand the spilled-body temp file to the client for streaming. Returns the
	// fd (positioned arbitrarily; caller must seek) and sets size, transferring
	// ownership so this handler no longer closes it. Returns -1 if the body was
	// small enough to stay in memory (served from the response body instead).
	bool				hasBodyFile() const;
	int					releaseBodyFile(size_t& size);

	// Setters
	void		setServerPort(int port);
	void		setScriptName(const std::string& scriptName);
	void		setPathInfo(const std::string& pathInfo, const std::string& pathTranslated);
	void		setRemoteAddr(const std::string& remoteAddr);

	// Utility methods
	static bool	isCgiRequest(const std::string& path, const Route& route);
	static std::string	getCgiExecutablePath(const std::string& extension, const Route& route);
};

#endif
