/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Logger.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: hmensah- <hmensah-@student.42abudhabi.a    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/08 22:20:20 by hmensah-          #+#    #+#             */
/*   Updated: 2026/06/08 22:20:58 by hmensah-         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../include/Logger.hpp"
#include <ctime>
#include <iomanip>
#include <sstream>

// Initialize static member
Logger* Logger::_instance = NULL;

// Private constructor
Logger::Logger() : _minLevel(INFO), _toConsole(true), _toFile(false) {
}

Logger::Logger(const Logger& other) {
	(void)other;
}

Logger& Logger::operator=(const Logger& other) {
	(void)other;
	return *this;
}

Logger::~Logger() {
	if (_logFile.is_open())
		_logFile.close();
}

// Singleton access
// Lazily heap-allocates the instance on first use; caller-managed lifetime, so
// destroy() must be invoked at shutdown to free it (no automatic teardown).
Logger* Logger::getInstance() {
	if (_instance == NULL)
		_instance = new Logger();
	return _instance;
}

void Logger::destroy() {
	if (_instance != NULL) {
		delete _instance;
		_instance = NULL;
	}
}

// Configuration
void Logger::setMinLevel(LogLevel level) {
	_minLevel = level;
}

// Opens the log file in append mode (closing any previously open one) so
// existing logs are preserved across runs.
void Logger::setLogFile(const std::string& filename) {
	if (_logFile.is_open())
		_logFile.close();
	_logFile.open(filename.c_str(), std::ios::out | std::ios::app);
}

void Logger::enableConsole(bool enable) {
	_toConsole = enable;
}

void Logger::enableFile(bool enable) {
	_toFile = enable;
}

// Logging methods
// Central gate: silently discards messages below the configured minimum level;
// everything else is formatted and written by writeLog.
void Logger::log(LogLevel level, const std::string& message) {
	if (level < _minLevel)
		return;
	writeLog(level, message);
}

void Logger::info(const std::string& message) {
	log(INFO, message);
}

void Logger::warning(const std::string& message) {
	log(WARNING, message);
}

void Logger::error(const std::string& message) {
	log(ERROR, message);
}

void Logger::fatal(const std::string& message) {
	log(FATAL, message);
}

// Private helper methods
std::string Logger::getLevelString(LogLevel level) const {
	switch (level) {
		case DEBUG: return "DEBUG";
		case INFO: return "INFO";
		case WARNING: return "WARNING";
		case ERROR: return "ERROR";
		case FATAL: return "FATAL";
		default: return "UNKNOWN";
	}
}

std::string Logger::getTimestamp() const {
	std::time_t now = std::time(NULL);
	std::tm* tmNow = std::localtime(&now);
	std::ostringstream oss;
	oss << std::setfill('0')
		<< (tmNow->tm_year + 1900) << "-"
		<< std::setw(2) << (tmNow->tm_mon + 1) << "-"
		<< std::setw(2) << tmNow->tm_mday << " "
		<< std::setw(2) << tmNow->tm_hour << ":"
		<< std::setw(2) << tmNow->tm_min << ":"
		<< std::setw(2) << tmNow->tm_sec;
	return oss.str();
}

// Formats the final "[timestamp] [LEVEL] message" line and fans it out to the
// enabled sinks; the file sink is flushed each write so logs aren't lost on crash.
void Logger::writeLog(LogLevel level, const std::string& message) {
	std::string line = "[" + getTimestamp() + "] [" + getLevelString(level) + "] " + message;
	if (_toConsole)
		std::cout << line << std::endl;
	if (_toFile && _logFile.is_open()) {
		_logFile << line << std::endl;
		_logFile.flush();
	}
}


