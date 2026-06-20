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
	Verbose,
	Info,
	Warning,
	Error
};

enum class LogVerbosity {
	Off,
	Normal,
	Verbose
};

class Logger {
public:
	static Logger& GetInstance() {
		static Logger instance;
		return instance;
	}

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

	void SetVerbosity(LogVerbosity verbosity) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_verbosity = verbosity;
	}

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
	Logger() = default;
	~Logger() {
		if (m_out.is_open()) {
			m_out.close();
		}
	}
	Logger(const Logger&) = delete;
	Logger& operator=(const Logger&) = delete;

	std::ofstream m_out;
	std::mutex m_mutex;
	LogVerbosity m_verbosity = LogVerbosity::Normal;
};
