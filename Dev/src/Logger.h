#pragma once

#include <string>
#include <fstream>
#include <iostream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>

enum class LogLevel {
	// Detailed diagnostics that are emitted only in verbose mode.
	Verbose,
	// Normal lifecycle and settings diagnostics.
	Info,
	// Recoverable failure or degraded-behavior diagnostics.
	Warning,
	// Failure diagnostics that affect requested behavior.
	Error
};

enum class LogVerbosity {
	// Suppresses all messages after the logger is initialized.
	Off,
	// Writes Info, Warning, and Error messages.
	Normal,
	// Writes every log level.
	Verbose
};

// Process-wide, thread-safe writer for plugin diagnostic messages.
class Logger {
public:
	// Returns the process-wide logger instance.
	static Logger& GetInstance() {
		static Logger instance;
		return instance;
	}

	// Closes the active stream, attempts to move the previous log to .last, then opens path.
	void Initialize(const std::string& logFilePath) {
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_out.is_open()) {
			m_out.close();
		}

		std::filesystem::path path(logFilePath);
		std::filesystem::path backupPath = path;
		backupPath.replace_extension(".last");

		std::error_code ec;
		if (std::filesystem::exists(path)) {
			if (std::filesystem::exists(backupPath)) {
				std::filesystem::remove(backupPath, ec);
			}
			std::filesystem::rename(path, backupPath, ec);
		}

		m_out.open(logFilePath, std::ios_base::out | std::ios_base::trunc);
	}

	// Changes the minimum verbosity used by WriteLine.
	void SetVerbosity(LogVerbosity verbosity) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_verbosity = verbosity;
	}

	// Writes a timestamped message to the open log file when its level is enabled.
	void WriteLine(LogLevel level, const std::string& message) {
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_verbosity == LogVerbosity::Off
			|| (level == LogLevel::Verbose && m_verbosity != LogVerbosity::Verbose)) {
			return;
		}

		if (m_out.is_open()) {
			std::string prefix;
			switch (level) {
				case LogLevel::Verbose: prefix = "[VERBOSE] "; break;
				case LogLevel::Info: prefix = "[INFO] "; break;
				case LogLevel::Warning: prefix = "[WARNING] "; break;
				case LogLevel::Error: prefix = "[ERROR] "; break;
			}
			
			auto now = std::chrono::system_clock::now();
			auto in_time_t = std::chrono::system_clock::to_time_t(now);
			
			struct tm timeinfo;
			localtime_s(&timeinfo, &in_time_t);
			
			std::stringstream ss;
			ss << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S");
			
			m_out << "[" << ss.str() << "] " << prefix << message << std::endl;
		}
	}

private:
	// Prevents direct construction outside the process-wide singleton.
	Logger() = default;
	// Closes the active log stream during process teardown.
	~Logger() {
		if (m_out.is_open()) {
			m_out.close();
		}
	}
	// Logger instances are unique and cannot be copied.
	Logger(const Logger&) = delete;
	// Logger instances are unique and cannot be assigned.
	Logger& operator=(const Logger&) = delete;

	// File stream receiving timestamped plugin diagnostics.
	std::ofstream m_out;
	// Guards the stream and verbosity from concurrent writes.
	std::mutex m_mutex;
	// Minimum verbosity currently accepted by WriteLine.
	LogVerbosity m_verbosity = LogVerbosity::Normal;
};
