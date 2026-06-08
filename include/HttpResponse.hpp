#ifndef HTTPRESPONSE_HPP
# define HTTPRESPONSE_HPP

# include <string>
# include <map>

class HttpResponse {
private:
	int									_statusCode;
	std::string							_statusMessage;
	std::string							_httpVersion;
	std::map<std::string, std::string>	_headers;
	std::string							_body;
	bool								_headersSent;

	// Helper methods
	std::string		getStatusMessage(int code) const;
	std::string		getHttpDate() const;
	void			setDefaultHeaders();

public:
	// Orthodox Canonical Form
	HttpResponse();
	HttpResponse(const HttpResponse& other);
	HttpResponse& operator=(const HttpResponse& other);
	~HttpResponse();

	// Setters
	void		setStatusCode(int code);
	void		setHttpVersion(const std::string& version);
	void		addHeader(const std::string& key, const std::string& value);
	void		setBody(const std::string& body);
	void		appendBody(const std::string& data);

	// Getters
	int									getStatusCode() const;
	const std::string&					getStatusMessage() const;
	const std::string&					getHttpVersion() const;
	const std::map<std::string, std::string>&	getHeaders() const;
	const std::string&					getBody() const;

	// Response building
	std::string		build();
	std::string		buildHeaders();
	void			clear();

	// Utility methods
	void		setContentType(const std::string& type);
	void		setContentLength(size_t length);
	void		setLocation(const std::string& location);
	void		setCookie(const std::string& name, const std::string& value);
	bool		isHeadersSent() const;

	// Error responses
	void		buildErrorResponse(int code, const std::string& errorPage);
};

#endif
