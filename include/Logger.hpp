#ifndef LOGGER_HPP
# define LOGGER_HPP

# include <string>
# include <fstream>
# include <iostream>

enum LogLevel {
	DEBUG,
	INFO,
	WARNING,
	ERROR,
	FATAL
};

// Process-wide singleton logger. Emits timestamped, level-tagged lines
// ("[YYYY-MM-DD HH:MM:SS] [LEVEL] message") to the console and/or a file, and
// drops anything below the configured minimum level. Not thread-safe.
class Logger {
private:
	static Logger*		_instance;
	std::ofstream		_logFile;
	LogLevel			_minLevel;
	bool				_toConsole;
	bool				_toFile;

	// Private constructor for singleton
	Logger();
	Logger(const Logger& other);
	Logger& operator=(const Logger& other);

	// Helper methods
	std::string		getLevelString(LogLevel level) const;
	std::string		getTimestamp() const;
	void			writeLog(LogLevel level, const std::string& message);

public:
	~Logger();

	// Singleton access
	static Logger*	getInstance();
	static void		destroy();

	// Configuration
	void		setMinLevel(LogLevel level);
	void		setLogFile(const std::string& filename);
	void		enableConsole(bool enable);
	void		enableFile(bool enable);

	// Logging methods
	void		log(LogLevel level, const std::string& message);
	void		info(const std::string& message);
	void		warning(const std::string& message);
	void		error(const std::string& message);
	void		fatal(const std::string& message);
};

#endif
