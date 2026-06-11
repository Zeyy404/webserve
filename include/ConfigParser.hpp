#ifndef CONFIGPARSER_HPP
# define CONFIGPARSER_HPP

# include <string>
# include <vector>
# include <fstream>
# include "ServerConfig.hpp"

class ConfigParser {
private:
	std::string					_configFile;
	std::vector<ServerConfig>	_serverConfigs;
	size_t						_currentLine;
	std::string					_currentLocationPath;

	// Parsing helper methods
	void		parseServerBlock(std::ifstream& file);
	void		parseLocationBlock(std::ifstream& file, ServerConfig& config);
	void		parseDirective(const std::string& line, ServerConfig& config);
	void		parseRouteDirective(const std::string& line, Route& route);
	std::string	trim(const std::string& str);
	bool		isComment(const std::string& line);
	bool		isEmpty(const std::string& line);

	// Validation
	void		validateConfig();
	void		validateServerConfig(const ServerConfig& config);

public:
	// Orthodox Canonical Form
	ConfigParser();
	ConfigParser(const std::string& configFile);
	ConfigParser(const ConfigParser& other);
	ConfigParser& operator=(const ConfigParser& other);
	~ConfigParser();

	// Parsing
	bool		parse();

	// Getters
	const std::vector<ServerConfig>&	getServerConfigs() const;

	// Utility methods
	static std::vector<std::string>		split(const std::string& str, char delimiter);
	static std::string					getDefaultConfigPath();
};

#endif
