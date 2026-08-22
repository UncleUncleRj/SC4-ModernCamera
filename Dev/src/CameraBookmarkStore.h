#pragma once

#include "SC4CameraController.h"

#include <array>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// Complete native view retained for one numbered bookmark.
struct CameraBookmark
{
	// Restorable native camera state.
	SC4NativeCameraState view{};
};

// Persists the eight numbered camera views for the active city. File writes
// are serialized on an owned worker so no render or input callback writes to
// disk directly.
class CameraBookmarkStore
{
public:
	// Number of fixed bookmark slots exposed by the UI and hotkeys.
	static constexpr uint32_t kSlotCount = 8;
	CameraBookmarkStore();
	~CameraBookmarkStore();

	// Sets the file path and starts the asynchronous writer. Calling it more
	// than once keeps the original initialized path.
	bool Initialize(const std::filesystem::path& storagePath);
	// Loads the city record identified by the stable region/city key.
	bool ActivateCity(const std::string& cityKey);
	// Stops exposing a city record while retaining data for later cities.
	void DeactivateCity();
	// Stops the writer and drains its final queued atomic save.
	void Shutdown();

	// Returns a copy of a populated slot, if the active city owns one.
	std::optional<CameraBookmark> Get(uint32_t slot) const;
	// Returns whether the active city has a populated slot.
	bool Has(uint32_t slot) const;
	// Stores a complete restorable view in one slot.
	bool Save(uint32_t slot, const SC4NativeCameraState& view);
	// Removes one populated slot after the caller has confirmed the action.
	bool Clear(uint32_t slot);

private:
	struct CityBookmarks
	{
		std::string key;
		std::array<std::optional<CameraBookmark>, kSlotCount> slots;
	};

	// Reads the versioned file once before the first city becomes active.
	bool Load();
	// Serializes the complete in-memory store after a mutation.
	std::vector<uint8_t> SerializeLocked() const;
	// Adds a complete serialized image to the worker's one-item write queue.
	void QueueWriteLocked();
	// Writes queued images to a temporary sibling, then atomically replaces the
	// previous file without involving SC4 services.
	void WriterLoop();
	// Finds the active city entry while the caller owns mutex.
	CityBookmarks* ActiveCityLocked();
	const CityBookmarks* ActiveCityLocked() const;
	// Checks the slot range before a value crosses the persistence boundary.
	static bool IsSlotValid(uint32_t slot);

	mutable std::mutex mutex;
	std::condition_variable writeCondition;
	std::filesystem::path path;
	std::vector<CityBookmarks> cities;
	std::string activeCityKey;
	std::vector<uint8_t> pendingWrite;
	std::thread writer;
	bool initialized = false;
	bool loaded = false;
	bool stopping = false;
};
