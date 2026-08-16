#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <deque>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

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

// Process-wide diagnostic writer that moves file I/O off the game thread after startup.
class Logger {
public:
	// Returns the process-lifetime logger instance, preserving worker state during an abortive exit.
	static Logger& GetInstance() {
		static Logger* instance = new Logger();
		return *instance;
	}

	// Opens a new log file and rotates the previous file before asynchronous logging starts.
	void Initialize(const std::string& logFilePath) {
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

	// Starts the background writer after a verified framework shutdown path is available.
	bool StartAsync() {
		if (!m_out.is_open()
			|| m_writerMode.load(std::memory_order_acquire) != WriterMode::Synchronous
			|| m_worker.joinable()) {
			return false;
		}

		HANDLE wakeEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
		if (!wakeEvent) {
			return false;
		}

		m_stopRequested.store(false, std::memory_order_release);
		m_workAvailableEvent = wakeEvent;
		m_workerRunning.store(true, std::memory_order_release);
		m_writerMode.store(WriterMode::Asynchronous, std::memory_order_release);

		try {
			m_worker = std::thread(&Logger::WriterMain, this);
		}
		catch (...) {
			m_writerMode.store(WriterMode::Synchronous, std::memory_order_release);
			m_workerRunning.store(false, std::memory_order_release);
			m_workAvailableEvent = nullptr;
			CloseHandle(wakeEvent);
			return false;
		}

		return true;
	}

	// Changes the minimum verbosity used by WriteLine.
	void SetVerbosity(LogVerbosity verbosity) {
		m_verbosity.store(verbosity, std::memory_order_release);
	}

	// Returns whether the current verbosity accepts the specified message level.
	bool IsEnabled(LogLevel level) const {
		const LogVerbosity verbosity = m_verbosity.load(std::memory_order_acquire);
		return verbosity != LogVerbosity::Off
			&& (level != LogLevel::Verbose || verbosity == LogVerbosity::Verbose);
	}

	// Builds a message only when the current verbosity would write it.
	template <typename MessageFactory>
	void WriteLineLazy(LogLevel level, MessageFactory&& createMessage) {
		if (!IsEnabled(level)) {
			return;
		}

		WriteLine(level, std::forward<MessageFactory>(createMessage)());
	}

	// Queues a timestamped message when asynchronous logging is active, or writes it during startup.
	void WriteLine(LogLevel level, std::string message) {
		if (!IsEnabled(level)) {
			return;
		}

		LogRecord record{
			std::chrono::system_clock::now(),
			level,
			std::move(message),
		};

		const WriterMode mode = m_writerMode.load(std::memory_order_acquire);
		if (mode == WriterMode::Asynchronous) {
			QueueRecord(std::move(record));
		}
		else if (mode == WriterMode::Synchronous) {
			WriteRecord(record);
			if (m_out.is_open()) {
				m_out.flush();
			}
		}
	}

	// Requests a graceful stop, drains queued records, and joins the writer thread.
	void Shutdown() {
		const WriterMode mode = m_writerMode.load(std::memory_order_acquire);
		if (mode == WriterMode::Synchronous) {
			if (m_out.is_open()) {
				m_out.flush();
				m_out.close();
			}
			m_writerMode.store(WriterMode::Stopped, std::memory_order_release);
			return;
		}

		if (mode != WriterMode::Asynchronous && mode != WriterMode::Stopping) {
			return;
		}

		m_writerMode.store(WriterMode::Stopping, std::memory_order_release);
		m_stopRequested.store(true, std::memory_order_release);
		if (m_workAvailableEvent) {
			SetEvent(m_workAvailableEvent);
		}

		if (m_worker.joinable()) {
			m_worker.join();
		}

	if (m_workAvailableEvent) {
		CloseHandle(m_workAvailableEvent);
		m_workAvailableEvent = nullptr;
	}
	if (m_out.is_open()) {
		m_out.close();
	}

	m_writerMode.store(WriterMode::Stopped, std::memory_order_release);
	}

	// Signals a stop without waiting when the framework reports an abortive quit.
	void RequestAbortiveStop() {
		if (m_writerMode.load(std::memory_order_acquire) != WriterMode::Asynchronous) {
			return;
		}

		m_writerMode.store(WriterMode::Stopping, std::memory_order_release);
		m_stopRequested.store(true, std::memory_order_release);
		if (m_workAvailableEvent) {
			SetEvent(m_workAvailableEvent);
		}
	}

	// Reports whether the DLL must remain loaded because the writer has not completed a controlled shutdown.
	bool CanUnloadNow() const {
		const WriterMode mode = m_writerMode.load(std::memory_order_acquire);
		return !m_workerRunning.load(std::memory_order_acquire)
			&& mode != WriterMode::Asynchronous
			&& mode != WriterMode::Stopping;
	}

private:
	enum class WriterMode : uint8_t {
		// Startup writes run on the caller before the worker exists.
		Synchronous,
		// The writer thread owns file output.
		Asynchronous,
		// The worker is draining records after a stop request.
		Stopping,
		// Graceful shutdown completed and the file is closed.
		Stopped,
	};

	// Immutable log data copied by the game thread before the worker writes it.
	struct LogRecord {
		std::chrono::system_clock::time_point timestamp;
		LogLevel level;
		std::string message;
	};

	// Maximum number of pending records retained without blocking the game thread.
	static constexpr std::size_t kQueueCapacity = 1024;
	// Maximum delay before the worker flushes records when a wake signal is missed.
	static constexpr DWORD kWorkerWakeIntervalMs = 250;

	// Prevents direct construction outside the process-lifetime singleton.
	Logger() = default;
	// Logger instances are unique and cannot be copied.
	Logger(const Logger&) = delete;
	// Logger instances are unique and cannot be assigned.
	Logger& operator=(const Logger&) = delete;

	// Attempts to enqueue one record without waiting for the worker's queue lock.
	void QueueRecord(LogRecord record) {
		std::unique_lock<std::mutex> lock(m_queueMutex, std::try_to_lock);
		if (!lock.owns_lock() || m_queue.size() >= kQueueCapacity) {
			m_droppedRecordCount.fetch_add(1, std::memory_order_relaxed);
			return;
		}

		m_queue.emplace_back(std::move(record));
		lock.unlock();
		if (m_workAvailableEvent) {
			SetEvent(m_workAvailableEvent);
		}
	}

	// Writes queued records and manages the log file without touching game state.
	void WriterMain() noexcept {
		try {
			for (;;) {
				WaitForSingleObject(m_workAvailableEvent, kWorkerWakeIntervalMs);
				DrainQueue();

				if (m_stopRequested.load(std::memory_order_acquire)) {
					DrainQueue();
					WriteDroppedRecordCount();
					break;
				}
			}
		}
		catch (...) {
			// An unexpected worker failure must not cross the thread boundary into std::terminate.
		}

		if (m_out.is_open()) {
			m_out.flush();
			m_out.close();
		}
		m_workerRunning.store(false, std::memory_order_release);
	}

	// Moves pending records into the worker and flushes their file output.
	void DrainQueue() {
		std::deque<LogRecord> records;
		{
			std::lock_guard<std::mutex> lock(m_queueMutex);
			records.swap(m_queue);
		}

		for (const LogRecord& record : records) {
			WriteRecord(record);
		}

		WriteDroppedRecordCount();
		if (m_out.is_open()) {
			m_out.flush();
		}
	}

	// Writes a summary for records dropped because the producer could not use the bounded queue.
	void WriteDroppedRecordCount() {
		const uint32_t droppedCount = m_droppedRecordCount.exchange(0, std::memory_order_acq_rel);
		if (droppedCount == 0) {
			return;
		}

		WriteRecord(LogRecord{
			std::chrono::system_clock::now(),
			LogLevel::Warning,
			"Async logger dropped " + std::to_string(droppedCount) + " record(s) because its queue was busy or full.",
		});
	}

	// Formats and writes one record to the file currently owned by the active writer.
	void WriteRecord(const LogRecord& record) {
		if (!m_out.is_open()) {
			return;
		}

		const char* prefix = "[INFO] ";
		switch (record.level) {
			case LogLevel::Verbose: prefix = "[VERBOSE] "; break;
			case LogLevel::Info: prefix = "[INFO] "; break;
			case LogLevel::Warning: prefix = "[WARNING] "; break;
			case LogLevel::Error: prefix = "[ERROR] "; break;
		}

		const std::time_t timestamp = std::chrono::system_clock::to_time_t(record.timestamp);
		std::tm timeInfo{};
		localtime_s(&timeInfo, &timestamp);

		std::ostringstream stream;
		stream << std::put_time(&timeInfo, "%Y-%m-%d %H:%M:%S");
		m_out << '[' << stream.str() << "] " << prefix << record.message << '\n';
	}

	// File stream receiving timestamped plugin diagnostics before startup and on the writer thread afterward.
	std::ofstream m_out;
	// Holds pending log records for the writer thread.
	std::deque<LogRecord> m_queue;
	// Guards the bounded producer queue.
	std::mutex m_queueMutex;
	// Wakes the writer when a record or stop request is queued.
	HANDLE m_workAvailableEvent = nullptr;
	// Writer thread joined during a graceful PreAppShutdown callback.
	std::thread m_worker;
	// Counts records dropped because the game thread never waits for queue access.
	std::atomic<uint32_t> m_droppedRecordCount{ 0 };
	// Signals that the writer must flush pending records and exit.
	std::atomic<bool> m_stopRequested{ false };
	// Tracks whether the worker is still executing plugin code.
	std::atomic<bool> m_workerRunning{ false };
	// Selects synchronous startup output, asynchronous output, or shutdown behavior.
	std::atomic<WriterMode> m_writerMode{ WriterMode::Synchronous };
	// Minimum verbosity currently accepted by WriteLine.
	std::atomic<LogVerbosity> m_verbosity{ LogVerbosity::Normal };
};
