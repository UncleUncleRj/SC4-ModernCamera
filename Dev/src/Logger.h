#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>

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
		m_verbosity.store(verbosity, std::memory_order_relaxed);
		if (m_out.is_open()) {
			m_out.flush();
		}
	}

	bool IsEnabled(LogLevel level) const {
		const LogVerbosity verbosity = m_verbosity.load(std::memory_order_relaxed);
		return verbosity != LogVerbosity::Off
			&& (level != LogLevel::Verbose || verbosity == LogVerbosity::Verbose);
	}

	template <typename MessageFactory>
	void WriteLineLazy(LogLevel level, MessageFactory&& createMessage) {
		if (!IsEnabled(level)) {
			return;
		}

		WriteLine(level, std::forward<MessageFactory>(createMessage)());
	}

	void WriteLine(LogLevel level, const std::string& message) {
		if (!IsEnabled(level)) {
			return;
		}

		std::lock_guard<std::mutex> lock(m_mutex);
		if (!IsEnabled(level)) {
			return;
		}

		if (m_out.is_open()) {
			const char* prefix = "";
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

			char timestamp[20]{};
			sprintf_s(
				timestamp,
				sizeof(timestamp),
				"%04d-%02d-%02d %02d:%02d:%02d",
				timeinfo.tm_year + 1900,
				timeinfo.tm_mon + 1,
				timeinfo.tm_mday,
				timeinfo.tm_hour,
				timeinfo.tm_min,
				timeinfo.tm_sec);

			m_out << "[" << timestamp << "] " << prefix << message << '\n';
			m_out.flush();
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
	std::atomic<LogVerbosity> m_verbosity = LogVerbosity::Normal;
};
