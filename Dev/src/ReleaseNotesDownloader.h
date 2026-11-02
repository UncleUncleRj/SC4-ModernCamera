#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

/// <summary>
/// Converts the release notes inside the Box-body element of a GitHub release
/// page to plain text, leaving out the title and author rows above them.
/// </summary>
/// <param name="html">Complete release page HTML.</param>
/// <returns>The text, or no value when the notes are missing or have no text.</returns>
std::optional<std::string> ExtractReleaseNotesText(std::string_view html);

/// <summary>Reads the version from a release URL such as ".../releases/tag/v1.0.4".</summary>
/// <param name="url">Final URL of the latest-release redirect.</param>
/// <returns>The digits and dots of the version, or no value for any other tag format.</returns>
std::optional<std::string> ExtractReleaseVersion(std::string_view url);

/// <summary>Compares dotted versions numerically; a missing component counts as zero.</summary>
/// <param name="candidate">Version that may be newer.</param>
/// <param name="current">Version to compare against.</param>
/// <returns>Whether candidate is a later version than current.</returns>
bool IsNewerVersion(std::string_view candidate, std::string_view current);

/// <summary>
/// Downloads the latest GitHub release's version and notes on an owned worker
/// so no game callback waits on the network. The worker never calls game, UI,
/// or logger APIs; the game thread polls for the result and reports it.
/// </summary>
class ReleaseNotesDownloader
{
public:
	/// <summary>Stops and joins any running download.</summary>
	~ReleaseNotesDownloader();

	/// <summary>
	/// Begins a download unless one is running, the version and notes are both
	/// known, or shutdown has begun. A failed download is retried by the next call.
	/// </summary>
	void Start();
	/// <summary>Returns whether a download is still running.</summary>
	bool IsDownloading() const;
	/// <summary>Returns a copy of the downloaded text, if a download has succeeded.</summary>
	std::optional<std::string> GetText() const;
	/// <summary>Returns a copy of the latest release's version, if one has been detected.</summary>
	std::optional<std::string> GetLatestVersion() const;
	/// <summary>Returns the last failure once, or an empty string when there is none.</summary>
	std::string ConsumeFailure();
	/// <summary>
	/// Stops further downloads and joins the worker. Network timeouts bound the
	/// wait because WinHTTP does not permit cancelling a synchronous request.
	/// </summary>
	void Shutdown();

private:
	/// <summary>Downloads and converts the page, then publishes the outcome.</summary>
	void WorkerMain();
	/// <summary>Follows the latest-release redirect and reads the release page.</summary>
	/// <param name="html">Receives the response body.</param>
	/// <param name="finalUrl">Receives the URL the redirect ended at.</param>
	/// <param name="error">Receives the failure description.</param>
	/// <returns>Whether the complete page was read.</returns>
	bool DownloadPage(std::string& html, std::string& finalUrl, std::string& error) const;

	// Guards every member below except stopping.
	mutable std::mutex mutex;
	// Worker for the current or most recent download.
	std::thread worker;
	// Set once by Shutdown; read by the worker between network reads.
	std::atomic<bool> stopping{ false };
	// True from Start until the worker has published its outcome.
	bool downloading = false;
	// Text from the successful download.
	std::optional<std::string> text;
	// Version read from the latest-release redirect.
	std::optional<std::string> latestVersion;
	// Description of the last failed download.
	std::string failure;
};
