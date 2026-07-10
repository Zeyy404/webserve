#include "../include/CgiHandler.hpp"
#include "../include/Logger.hpp"
#include "../include/HttpUtils.hpp"
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <limits.h>

using HttpUtils::trimWhitespace;
using HttpUtils::baseName;
using HttpUtils::dirName;

namespace {
	static const size_t SPILL_THRESHOLD = 1024 * 1024;

	static std::string toString(size_t value) {
		std::ostringstream oss;
		oss << value;
		return oss.str();
	}

	// Returns the file extension including the leading dot, or "" — a dot that
	// belongs to a parent directory (before the last slash) does not count.
	static std::string extensionWithDot(const std::string& path) {
		size_t slash = path.find_last_of('/');
		size_t dot = path.find_last_of('.');
		if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
			return "";
		return path.substr(dot);
	}

	// Maps an HTTP header name to its CGI meta-variable form: HTTP_ prefix,
	// uppercased, dashes turned into underscores (e.g. Accept-Encoding ->
	// HTTP_ACCEPT_ENCODING).
	static std::string envHeaderName(const std::string& header) {
		std::string out = "HTTP_";
		for (size_t i = 0; i < header.size(); ++i) {
			char c = header[i];
			if (c >= 'a' && c <= 'z')
				c = static_cast<char>(c - 'a' + 'A');
			else if (c == '-')
				c = '_';
			out += c;
		}
		return out;
	}
}

// Orthodox Canonical Form
CgiHandler::CgiHandler(HttpRequest& request, HttpResponse& response) : _request(request), _response(response), _exitStatus(0), _pid(-1), _inputFd(-1), _outputFd(-1), _inputOpen(false), _outputOpen(false), _processDone(false), _inputWritten(0), _serverPort(80), _startTime(0), _bodyFd(-1), _bodyBytes(0), _headersLocated(false), _bodyStart(0) {
}

CgiHandler::CgiHandler(const CgiHandler& other) : _request(other._request), _response(other._response) {
	_scriptPath = other._scriptPath;
	_cgiExecutable = other._cgiExecutable;
	_env = other._env;
	_output = other._output;
	_exitStatus = other._exitStatus;
	_pid = other._pid;
	_inputFd = other._inputFd;
	_outputFd = other._outputFd;
	_inputOpen = other._inputOpen;
	_outputOpen = other._outputOpen;
	_processDone = other._processDone;
	_inputWritten = other._inputWritten;
	_startTime = other._startTime;
	_serverPort = other._serverPort;
	_scriptName = other._scriptName;
	_pathInfo = other._pathInfo;
	_pathTranslated = other._pathTranslated;
	_remoteAddr = other._remoteAddr;
	_bodyFd = -1;
	_bodyBytes = other._bodyBytes;
	_headersLocated = other._headersLocated;
	_bodyStart = other._bodyStart;
}

// Copies CGI state but not the temp-file fd — _bodyFd is reset to -1 so a copy
// never closes the original's spilled-body file.
CgiHandler& CgiHandler::operator=(const CgiHandler& other) {
	if (this != &other) {
		_scriptPath = other._scriptPath;
		_cgiExecutable = other._cgiExecutable;
		_env = other._env;
		_output = other._output;
		_exitStatus = other._exitStatus;
		_pid = other._pid;
		_inputFd = other._inputFd;
		_outputFd = other._outputFd;
		_inputOpen = other._inputOpen;
		_outputOpen = other._outputOpen;
		_processDone = other._processDone;
		_inputWritten = other._inputWritten;
		_startTime = other._startTime;
		_serverPort = other._serverPort;
		_scriptName = other._scriptName;
		_pathInfo = other._pathInfo;
		_pathTranslated = other._pathTranslated;
		_remoteAddr = other._remoteAddr;
		_bodyFd = -1;
		_bodyBytes = other._bodyBytes;
		_headersLocated = other._headersLocated;
		_bodyStart = other._bodyStart;
	}
	return *this;
}

// Closes both pipe ends and the spilled-body temp file. Does not reap the
// child — killProcess()/finish() own that.
CgiHandler::~CgiHandler() {
	closeInput();
	closeOutput();
	closeBodyFile();
}

// CGI execution
// Resolves the interpreter, builds the environment, then forks and execs the
// child with stdin/stdout wired to two pipes. The parent keeps the write end of
// stdin and read end of stdout, both non-blocking. Returns false (having set an
// error response) if the interpreter is missing/non-executable or a syscall
// fails; the child's cwd is chdir'd to the script directory before execve.
bool CgiHandler::start(const std::string& scriptPath, const Route& route) {
	_scriptPath = scriptPath;
	_cgiExecutable = getCgiExecutablePath(extensionWithDot(scriptPath), route);
	if (!_cgiExecutable.empty() && _cgiExecutable[0] != '/') {
		char resolved[PATH_MAX];
		if (::realpath(_cgiExecutable.c_str(), resolved) != NULL)
			_cgiExecutable = resolved;
	}
	_output.clear();
	_exitStatus = 0;
	_pid = -1;
	_inputFd = -1;
	_outputFd = -1;
	_inputOpen = false;
	_outputOpen = false;
	_processDone = false;
	_inputWritten = 0;
	_startTime = std::time(NULL);
	closeBodyFile();
	_bodyBytes = 0;
	_headersLocated = false;
	_bodyStart = 0;

	if (_cgiExecutable.empty()) {
		_response.setStatusCode(404);
		_response.setContentType("text/html");
		_response.setBody("<html><body><h1>404 Not Found</h1></body></html>");
		return false;
	}
	if (::access(_cgiExecutable.c_str(), X_OK) != 0) {
		_response.setStatusCode(502);
		_response.setContentType("text/html");
		_response.setBody("<html><body><h1>502 Bad Gateway</h1></body></html>");
		return false;
	}

	setupEnvironment(route);

	int inputPipe[2];
	int outputPipe[2];
	if (::pipe(inputPipe) != 0)
		return false;
	if (::pipe(outputPipe) != 0) {
		::close(inputPipe[0]);
		::close(inputPipe[1]);
		return false;
	}

	pid_t pid = ::fork();
	if (pid < 0) {
		::close(inputPipe[0]);
		::close(inputPipe[1]);
		::close(outputPipe[0]);
		::close(outputPipe[1]);
		return false;
	}

	if (pid == 0) {
		::dup2(inputPipe[0], STDIN_FILENO);
		::dup2(outputPipe[1], STDOUT_FILENO);
		::close(inputPipe[0]);
		::close(inputPipe[1]);
		::close(outputPipe[0]);
		::close(outputPipe[1]);

		std::string scriptDir = dirName(_scriptPath);
		std::string scriptName = baseName(_scriptPath);
		::chdir(scriptDir.c_str());

		char** env = getEnvArray();
		char* argv[3];
		argv[0] = const_cast<char*>(_cgiExecutable.c_str());
		argv[1] = const_cast<char*>(scriptName.c_str());
		argv[2] = NULL;
		::execve(_cgiExecutable.c_str(), argv, env);
		freeEnvArray(env);
		_exit(1);
	}

	::close(inputPipe[0]);
	::close(outputPipe[1]);
	::fcntl(inputPipe[1], F_SETFL, O_NONBLOCK);
	::fcntl(outputPipe[0], F_SETFL, O_NONBLOCK);
	_pid = pid;
	_inputFd = inputPipe[1];
	_outputFd = outputPipe[0];
	_inputOpen = true;
	_outputOpen = true;
	if (_request.getBody().empty())
		closeInput();

	std::ostringstream oss;
	oss << "CGI started [script=" << _scriptPath << ", interpreter=" << _cgiExecutable << ", pid=" << _pid << "]";
	Logger::getInstance()->info(oss.str());
	return true;
}

// Writes one chunk (<=64KB) of the request body to the child's stdin. Closes
// stdin once the whole body is sent (giving the child EOF) and drops the body
// buffer. On a write error, reaps the child if it already exited and closes
// stdin so a dead child doesn't stall the loop; otherwise the write is retried.
ssize_t CgiHandler::writeInput() {
	if (!_inputOpen || _inputFd < 0)
		return 0;
	const std::string& input = _request.getBody();
	if (_inputWritten >= input.size()) {
		closeInput();
		return 0;
	}
	size_t remaining = input.size() - _inputWritten;
	size_t chunkSize = remaining > 65536 ? 65536 : remaining;
	ssize_t bytes = ::write(_inputFd, input.c_str() + _inputWritten, chunkSize);
	if (bytes > 0) {
		_inputWritten += static_cast<size_t>(bytes);
		_startTime = std::time(NULL);
		if (_inputWritten >= input.size()) {
			closeInput();
			_request.clearBody();
		}
		return bytes;
	}
	if (bytes < 0) {
		if (!_processDone && _pid > 0 && ::waitpid(_pid, &_exitStatus, WNOHANG) == _pid)
			_processDone = true;
		if (_processDone)
			closeInput();
	}
	return bytes;
}

// Reads one chunk of the child's stdout. Buffers into _output until the CGI
// header/body separator is found; once the accumulated body exceeds
// SPILL_THRESHOLD it is flushed to the temp file and subsequent reads append
// straight to that file, keeping only the header block in memory. Closes the
// output pipe on EOF (bytes == 0).
ssize_t CgiHandler::readOutput() {
	if (!_outputOpen || _outputFd < 0)
		return 0;
	char buffer[65536];
	ssize_t bytes = ::read(_outputFd, buffer, sizeof(buffer));
	if (bytes > 0) {
		_startTime = std::time(NULL);
		if (_bodyFd >= 0) {
			if (::write(_bodyFd, buffer, static_cast<size_t>(bytes)) == bytes)
				_bodyBytes += static_cast<size_t>(bytes);
			else
				closeBodyFile();
			return bytes;
		}
		_output.append(buffer, static_cast<size_t>(bytes));
		if (!_headersLocated) {
			size_t end = _output.find("\r\n\r\n");
			size_t sep = 4;
			if (end == std::string::npos) {
				end = _output.find("\n\n");
				sep = 2;
			}
			if (end != std::string::npos) {
				_headersLocated = true;
				_bodyStart = end + sep;
			}
		}
		if (_headersLocated && _output.size() - _bodyStart > SPILL_THRESHOLD) {
			openBodyFile();
			if (_bodyFd >= 0) {
				size_t len = _output.size() - _bodyStart;
				if (::write(_bodyFd, _output.c_str() + _bodyStart, len) == static_cast<ssize_t>(len)) {
					_bodyBytes = len;
					_output.resize(_bodyStart);
				} else {
					closeBodyFile();
				}
			}
		}
	} else if (bytes == 0)
		closeOutput();
	return bytes;
}

// Opens a private temp file for the spilled body and immediately unlinks it, so
// the fd is the only handle and the file vanishes automatically when closed.
void CgiHandler::openBodyFile() {
	if (_bodyFd >= 0)
		return;
	static unsigned long counter = 0;
	std::ostringstream oss;
	oss << "/tmp/webserv_cgi_" << static_cast<long>(_pid) << "_" << counter++;
	_bodyFd = ::open(oss.str().c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
	if (_bodyFd >= 0)
		::unlink(oss.str().c_str());
}

void CgiHandler::closeBodyFile() {
	if (_bodyFd >= 0)
		::close(_bodyFd);
	_bodyFd = -1;
}

bool CgiHandler::hasBodyFile() const {
	return _bodyFd >= 0;
}

// Transfers ownership of the temp-file fd to the caller: clears _bodyFd so this
// handler won't close it, and reports the body size via the out-param.
int CgiHandler::releaseBodyFile(size_t& size) {
	int fd = _bodyFd;
	size = _bodyBytes;
	_bodyFd = -1;
	return fd;
}

// Closes the stdin pipe write end, signalling EOF to the child's stdin.
void CgiHandler::closeInput() {
	if (_inputFd >= 0)
		::close(_inputFd);
	_inputFd = -1;
	_inputOpen = false;
}

void CgiHandler::closeOutput() {
	if (_outputFd >= 0)
		::close(_outputFd);
	_outputFd = -1;
	_outputOpen = false;
}

// Non-blocking check that the CGI is fully done: reaps the child if it exited,
// and reports complete only once both pipes are closed and the child reaped.
bool CgiHandler::isComplete() {
	if (_pid > 0 && !_processDone) {
		pid_t waited = ::waitpid(_pid, &_exitStatus, WNOHANG);
		if (waited == _pid)
			_processDone = true;
	}
	return !_inputOpen && !_outputOpen && _processDone;
}

bool CgiHandler::isTimeout(time_t now, time_t timeout) const {
	return _pid > 0 && (now - _startTime) > timeout;
}

// Force-kills the child with SIGKILL, reaps it (blocking) to avoid a zombie,
// and closes both pipes and the temp file. Safe to call repeatedly.
void CgiHandler::killProcess() {
	if (_pid > 0 && !_processDone) {
		::kill(_pid, SIGKILL);
		::waitpid(_pid, &_exitStatus, 0);
		_processDone = true;
		std::ostringstream oss;
		oss << "CGI killed [script=" << _scriptPath << ", pid=" << _pid << "]";
		Logger::getInstance()->warning(oss.str());
	}
	closeInput();
	closeOutput();
	closeBodyFile();
}

// Called once the CGI is complete: reaps the child, maps a non-zero exit with
// no output to a 502, otherwise parses the CGI output into the response and, if
// the body was spilled, records the temp-file length as the external body size.
void CgiHandler::finish() {
	if (_pid > 0 && !_processDone) {
		pid_t waited = ::waitpid(_pid, &_exitStatus, WNOHANG);
		if (waited == _pid)
			_processDone = true;
	}
	if (WIFEXITED(_exitStatus) && WEXITSTATUS(_exitStatus) != 0 && _output.empty()) {
		std::ostringstream oss;
		oss << "CGI failed [script=" << _scriptPath << ", pid=" << _pid
			<< ", exit=" << WEXITSTATUS(_exitStatus) << "]";
		Logger::getInstance()->error(oss.str());
		setGatewayError(502);
		return;
	}
	parseOutput();
	if (_bodyFd >= 0)
		_response.setExternalBodyLength(_bodyBytes);
}

// Replaces the response with a gateway error page (504 for timeout, else 502).
void CgiHandler::setGatewayError(int statusCode) {
	_response.setStatusCode(statusCode);
	_response.setContentType("text/html");
	if (statusCode == 504)
		_response.setBody("<html><body><h1>504 Gateway Timeout</h1></body></html>");
	else
		_response.setBody("<html><body><h1>502 Bad Gateway</h1></body></html>");
}

void CgiHandler::setEnvVariable(const std::string& key, const std::string& value) {
	_env[key] = value;
}

// Private helper methods
// Builds the CGI/1.1 meta-variable environment: the standard variables (method,
// URI, script/path info, content length/type, server name/port, remote addr)
// plus every request header re-exported as an HTTP_* variable.
void CgiHandler::setupEnvironment(const Route& route) {
	(void)route;
	_env.clear();
	_env["GATEWAY_INTERFACE"] = "CGI/1.1";
	_env["SERVER_SOFTWARE"] = "webserv/0.1";
	_env["SERVER_PROTOCOL"] = _request.getHttpVersion();
	_env["SERVER_PORT"] = toString(_serverPort);
	_env["REQUEST_METHOD"] = _request.getMethod();
	_env["REQUEST_URI"] = _request.getUri();
	_env["SCRIPT_NAME"] = _scriptName.empty() ? _request.getPath() : _scriptName;
	_env["SCRIPT_FILENAME"] = _scriptPath;
	_env["QUERY_STRING"] = _request.getQueryString();
	_env["PATH_INFO"] = _pathInfo;
	_env["PATH_TRANSLATED"] = _pathInfo.empty() ? "" : _pathTranslated;
	if (!_remoteAddr.empty())
		_env["REMOTE_ADDR"] = _remoteAddr;
	_env["REDIRECT_STATUS"] = "200";
	_env["CONTENT_LENGTH"] = toString(_request.getBody().size());
	if (_request.hasHeader("content-type"))
		_env["CONTENT_TYPE"] = _request.getHeader("content-type");
	if (_request.hasHeader("host")) {
		std::string host = _request.getHeader("host");
		size_t colon = host.find(':');
		if (colon == std::string::npos) {
			_env["SERVER_NAME"] = host;
		} else {
			_env["SERVER_NAME"] = host.substr(0, colon);
		}
	} else {
		_env["SERVER_NAME"] = "localhost";
	}

	const std::map<std::string, std::string>& headers = _request.getHeaders();
	for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it) {
		if (it->first == "content-type" || it->first == "content-length")
			continue;
		_env[envHeaderName(it->first)] = it->second;
	}
}

// Splits the CGI output into its header block and body. Honours Status and
// Content-Type headers, drops Content-Length (recomputed by the response), and
// forwards the rest. With no header separator the whole output is treated as a
// text/plain 200 body. The body is moved (swapped) into the response, not copied.
void CgiHandler::parseOutput() {
	size_t headerEnd = _output.find("\r\n\r\n");
	size_t sepLength = 4;
	if (headerEnd == std::string::npos) {
		headerEnd = _output.find("\n\n");
		sepLength = 2;
	}

	if (headerEnd == std::string::npos) {
		_response.setStatusCode(200);
		_response.setContentType("text/plain");
		_response.setBodySwap(_output);
		return;
	}

	std::string headerBlock = _output.substr(0, headerEnd);
	std::istringstream iss(headerBlock);
	std::string line;
	bool hasContentType = false;
	_response.setStatusCode(200);

	while (std::getline(iss, line)) {
		line = trimWhitespace(line);
		if (line.empty())
			continue;
		size_t colon = line.find(':');
		if (colon == std::string::npos)
			continue;
		std::string key = trimWhitespace(line.substr(0, colon));
		std::string value = trimWhitespace(line.substr(colon + 1));
		if (key == "Status") {
			_response.setStatusCode(std::atoi(value.c_str()));
		} else if (key == "Content-Type") {
			_response.setContentType(value);
			hasContentType = true;
		} else if (key != "Content-Length") {
			_response.addHeader(key, value);
		}
	}

	if (!hasContentType)
		_response.setContentType("text/plain");
	_output.erase(0, headerEnd + sepLength);
	_response.setBodySwap(_output);
}

// Flattens _env into a NULL-terminated "KEY=VALUE" array for execve. Caller
// owns the result and must release it with freeEnvArray.
char** CgiHandler::getEnvArray() const {
	char** env = new char*[_env.size() + 1];
	size_t i = 0;
	for (std::map<std::string, std::string>::const_iterator it = _env.begin(); it != _env.end(); ++it) {
		std::string entry = it->first + "=" + it->second;
		env[i] = new char[entry.size() + 1];
		std::strcpy(env[i], entry.c_str());
		++i;
	}
	env[i] = NULL;
	return env;
}

void CgiHandler::freeEnvArray(char** env) const {
	if (env == NULL)
		return;
	for (size_t i = 0; env[i] != NULL; ++i)
		delete[] env[i];
	delete[] env;
}

// Getters
int CgiHandler::getInputFd() const {
	return _inputFd;
}

int CgiHandler::getOutputFd() const {
	return _outputFd;
}

bool CgiHandler::wantsInputWrite() const {
	return _inputOpen && _inputFd >= 0;
}

bool CgiHandler::wantsOutputRead() const {
	return _outputOpen && _outputFd >= 0;
}

// Setters
void CgiHandler::setServerPort(int port) {
	_serverPort = port;
}

void CgiHandler::setScriptName(const std::string& scriptName) {
	_scriptName = scriptName;
}

void CgiHandler::setPathInfo(const std::string& pathInfo, const std::string& pathTranslated) {
	_pathInfo = pathInfo;
	_pathTranslated = pathTranslated;
}

void CgiHandler::setRemoteAddr(const std::string& remoteAddr) {
	_remoteAddr = remoteAddr;
}

// Static utility methods
// True if the path's extension is configured as a CGI handler on the route.
bool CgiHandler::isCgiRequest(const std::string& path, const Route& route) {
	std::string ext = extensionWithDot(path);
	return !ext.empty() && route.hasCgiExtension(ext);
}

std::string CgiHandler::getCgiExecutablePath(const std::string& extension, const Route& route) {
	return route.getCgiHandler(extension);
}
