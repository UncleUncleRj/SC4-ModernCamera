#include "CameraBookmarkStore.h"

#include "Logger.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iterator>
#include <type_traits>

namespace
{
	constexpr uint32_t kFileMagic = 0x4D424353; // "SCBM"
	constexpr uint32_t kFileVersion = 2;
	constexpr uint32_t kLegacyThumbnailFileVersion = 1;
	constexpr uint32_t kLegacyThumbnailWidth = 160;
	constexpr uint32_t kLegacyThumbnailHeight = 90;
	constexpr uint32_t kLegacyThumbnailBytes = kLegacyThumbnailWidth * kLegacyThumbnailHeight * 4;
	constexpr uint32_t kMaximumCities = 4096;
	constexpr uint32_t kMaximumCityKeyBytes = 4096;

	template <typename T>
	void Append(std::vector<uint8_t>& bytes, const T& value)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		const size_t offset = bytes.size();
		bytes.resize(offset + sizeof(T));
		std::memcpy(bytes.data() + offset, &value, sizeof(T));
	}

	template <typename T>
	bool Read(const std::vector<uint8_t>& bytes, size_t& offset, T& value)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) {
			return false;
		}
		std::memcpy(&value, bytes.data() + offset, sizeof(T));
		offset += sizeof(T);
		return true;
	}

	void AppendVector(std::vector<uint8_t>& bytes, const cS3DVector3& vector)
	{
		Append(bytes, vector.fX);
		Append(bytes, vector.fY);
		Append(bytes, vector.fZ);
	}

	bool ReadVector(const std::vector<uint8_t>& bytes, size_t& offset, cS3DVector3& vector)
	{
		return Read(bytes, offset, vector.fX)
			&& Read(bytes, offset, vector.fY)
			&& Read(bytes, offset, vector.fZ);
	}

	void AppendView(std::vector<uint8_t>& bytes, const SC4NativeCameraState& view)
	{
		AppendVector(bytes, view.cameraPlaneOrigin);
		AppendVector(bytes, view.cameraPosition);
		AppendVector(bytes, view.viewTargetPosition);
		AppendVector(bytes, view.baseTargetForRotation);
		AppendVector(bytes, view.cameraOffset);
		Append(bytes, view.customMagnification);
		Append(bytes, view.prevZoom);
		Append(bytes, view.prevRotation);
		Append(bytes, view.zoom);
		Append(bytes, view.rotation);
		Append(bytes, view.postZoom);
		Append(bytes, view.postRotation);
		Append(bytes, view.yaw);
		Append(bytes, view.pitch);
		Append(bytes, view.cameraDistance);
		Append(bytes, view.nearClip);
		Append(bytes, view.farClip);
		Append(bytes, view.orthoScale);
		Append(bytes, view.invOrthoScale);
	}

	bool ReadView(const std::vector<uint8_t>& bytes, size_t& offset, SC4NativeCameraState& view)
	{
		return ReadVector(bytes, offset, view.cameraPlaneOrigin)
			&& ReadVector(bytes, offset, view.cameraPosition)
			&& ReadVector(bytes, offset, view.viewTargetPosition)
			&& ReadVector(bytes, offset, view.baseTargetForRotation)
			&& ReadVector(bytes, offset, view.cameraOffset)
			&& Read(bytes, offset, view.customMagnification)
			&& Read(bytes, offset, view.prevZoom)
			&& Read(bytes, offset, view.prevRotation)
			&& Read(bytes, offset, view.zoom)
			&& Read(bytes, offset, view.rotation)
			&& Read(bytes, offset, view.postZoom)
			&& Read(bytes, offset, view.postRotation)
			&& Read(bytes, offset, view.yaw)
			&& Read(bytes, offset, view.pitch)
			&& Read(bytes, offset, view.cameraDistance)
			&& Read(bytes, offset, view.nearClip)
			&& Read(bytes, offset, view.farClip)
			&& Read(bytes, offset, view.orthoScale)
			&& Read(bytes, offset, view.invOrthoScale);
	}

	bool IsFiniteVector(const cS3DVector3& vector)
	{
		return std::isfinite(vector.fX)
			&& std::isfinite(vector.fY)
			&& std::isfinite(vector.fZ);
	}

	bool IsFiniteView(const SC4NativeCameraState& view)
	{
		return IsFiniteVector(view.cameraPlaneOrigin)
			&& IsFiniteVector(view.cameraPosition)
			&& IsFiniteVector(view.viewTargetPosition)
			&& IsFiniteVector(view.baseTargetForRotation)
			&& IsFiniteVector(view.cameraOffset)
			&& std::isfinite(view.customMagnification)
			&& std::isfinite(view.yaw)
			&& std::isfinite(view.pitch)
			&& std::isfinite(view.cameraDistance)
			&& std::isfinite(view.nearClip)
			&& std::isfinite(view.farClip)
			&& std::isfinite(view.orthoScale)
			&& std::isfinite(view.invOrthoScale);
	}

	std::vector<uint8_t> ReadFile(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		if (!stream) {
			return {};
		}
		return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
	}
}

CameraBookmarkStore::CameraBookmarkStore() = default;

CameraBookmarkStore::~CameraBookmarkStore()
{
	Shutdown();
}

bool CameraBookmarkStore::Initialize(const std::filesystem::path& storagePath)
{
	std::lock_guard lock(mutex);
	if (initialized) {
		return path == storagePath;
	}

	path = storagePath;
	initialized = !path.empty();
	if (!initialized) {
		return false;
	}

	writer = std::thread(&CameraBookmarkStore::WriterLoop, this);
	return true;
}

bool CameraBookmarkStore::ActivateCity(const std::string& cityKey)
{
	if (cityKey.empty()) {
		return false;
	}
	bool needsLoad = false;
	{
		std::lock_guard lock(mutex);
		if (!initialized) {
			return false;
		}
		needsLoad = !loaded;
	}
	if (needsLoad && !Load()) {
		return false;
	}

	std::lock_guard lock(mutex);
	if (!initialized) {
		return false;
	}
	activeCityKey = cityKey;
	if (!ActiveCityLocked()) {
		cities.push_back({ activeCityKey, {} });
	}
	return true;
}

void CameraBookmarkStore::DeactivateCity()
{
	std::lock_guard lock(mutex);
	activeCityKey.clear();
}

void CameraBookmarkStore::Shutdown()
{
	{
		std::lock_guard lock(mutex);
		if (!initialized || stopping) {
			return;
		}
		stopping = true;
	}
	writeCondition.notify_one();
	if (writer.joinable()) {
		writer.join();
	}
}

std::optional<CameraBookmark> CameraBookmarkStore::Get(uint32_t slot) const
{
	if (!IsSlotValid(slot)) {
		return std::nullopt;
	}
	std::lock_guard lock(mutex);
	const CityBookmarks* city = ActiveCityLocked();
	return city ? city->slots[slot] : std::nullopt;
}

bool CameraBookmarkStore::Has(uint32_t slot) const
{
	return Get(slot).has_value();
}

bool CameraBookmarkStore::Save(
	uint32_t slot,
	const SC4NativeCameraState& view)
{
	if (!IsSlotValid(slot) || !IsFiniteView(view)) {
		return false;
	}
	std::lock_guard lock(mutex);
	CityBookmarks* city = ActiveCityLocked();
	if (!city) {
		return false;
	}
	city->slots[slot] = CameraBookmark{ view };
	QueueWriteLocked();
	return true;
}

bool CameraBookmarkStore::Clear(uint32_t slot)
{
	if (!IsSlotValid(slot)) {
		return false;
	}
	std::lock_guard lock(mutex);
	CityBookmarks* city = ActiveCityLocked();
	if (!city || !city->slots[slot]) {
		return false;
	}
	city->slots[slot].reset();
	QueueWriteLocked();
	return true;
}

bool CameraBookmarkStore::Load()
{
	std::filesystem::path storagePath;
	{
		std::lock_guard lock(mutex);
		if (!initialized) {
			return false;
		}
		if (loaded) {
			return true;
		}
		storagePath = path;
	}

	const std::vector<uint8_t> bytes = ReadFile(storagePath);
	std::vector<CityBookmarks> loadedCities;
	bool valid = bytes.empty();
	if (!bytes.empty()) {
		size_t offset = 0;
		uint32_t magic = 0;
		uint32_t version = 0;
		uint32_t cityCount = 0;
		valid = Read(bytes, offset, magic)
			&& Read(bytes, offset, version)
			&& Read(bytes, offset, cityCount)
			&& magic == kFileMagic
			&& (version == kFileVersion || version == kLegacyThumbnailFileVersion)
			&& cityCount <= kMaximumCities;

		for (uint32_t cityIndex = 0; valid && cityIndex < cityCount; ++cityIndex) {
			uint32_t keyLength = 0;
			CityBookmarks city;
			valid = Read(bytes, offset, keyLength)
				&& keyLength > 0
				&& keyLength <= kMaximumCityKeyBytes
				&& offset <= bytes.size()
				&& bytes.size() - offset >= keyLength;
			if (!valid) {
				break;
			}
			city.key.assign(reinterpret_cast<const char*>(bytes.data() + offset), keyLength);
			offset += keyLength;

			for (uint32_t slot = 0; valid && slot < kSlotCount; ++slot) {
				uint8_t populated = 0;
				valid = Read(bytes, offset, populated) && populated <= 1;
				if (!valid || populated == 0) {
					continue;
				}
				CameraBookmark bookmark;
				valid = ReadView(bytes, offset, bookmark.view)
					&& IsFiniteView(bookmark.view);
				if (valid && version == kLegacyThumbnailFileVersion) {
					uint32_t thumbnailWidth = 0;
					uint32_t thumbnailHeight = 0;
					uint32_t pixelCount = 0;
					valid = Read(bytes, offset, thumbnailWidth)
						&& Read(bytes, offset, thumbnailHeight)
						&& Read(bytes, offset, pixelCount)
						&& thumbnailWidth == kLegacyThumbnailWidth
						&& thumbnailHeight == kLegacyThumbnailHeight
						&& pixelCount == kLegacyThumbnailBytes
						&& offset <= bytes.size()
						&& bytes.size() - offset >= pixelCount;
					if (valid) {
						offset += pixelCount;
					}
				}
				if (!valid) {
					break;
				}
				city.slots[slot] = std::move(bookmark);
			}
			loadedCities.push_back(std::move(city));
		}
		valid = valid && offset == bytes.size();
	}

	std::lock_guard lock(mutex);
	loaded = true;
	if (!valid) {
		Logger::GetInstance().WriteLine(
			LogLevel::Warning,
			"Camera Bookmarks: ignored malformed or unsupported bookmark file; a new file will be created after the next change.");
		cities.clear();
		return true;
	}
	cities = std::move(loadedCities);
	return true;
}

std::vector<uint8_t> CameraBookmarkStore::SerializeLocked() const
{
	std::vector<uint8_t> bytes;
	bytes.reserve(12 + cities.size() * (8 + kSlotCount * (1 + sizeof(SC4NativeCameraState))));
	Append(bytes, kFileMagic);
	Append(bytes, kFileVersion);
	Append(bytes, static_cast<uint32_t>(cities.size()));
	for (const CityBookmarks& city : cities) {
		Append(bytes, static_cast<uint32_t>(city.key.size()));
		bytes.insert(bytes.end(), city.key.begin(), city.key.end());
		for (const std::optional<CameraBookmark>& slot : city.slots) {
			const uint8_t populated = slot ? 1 : 0;
			Append(bytes, populated);
			if (!slot) {
				continue;
			}
			AppendView(bytes, slot->view);
		}
	}
	return bytes;
}

void CameraBookmarkStore::QueueWriteLocked()
{
	pendingWrite = SerializeLocked();
	writeCondition.notify_one();
}

void CameraBookmarkStore::WriterLoop()
{
	for (;;) {
		std::vector<uint8_t> bytes;
		std::filesystem::path storagePath;
		{
			std::unique_lock lock(mutex);
			writeCondition.wait(lock, [this]() { return stopping || !pendingWrite.empty(); });
			if (stopping && pendingWrite.empty()) {
				return;
			}
			bytes = std::move(pendingWrite);
			pendingWrite.clear();
			storagePath = path;
		}

		const std::filesystem::path temporaryPath = storagePath.wstring() + L".tmp";
		std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
		if (!stream) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Camera Bookmarks: could not open temporary storage file.");
			continue;
		}
		stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		stream.close();
		if (!stream) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Camera Bookmarks: could not write temporary storage file.");
			continue;
		}

		if (!MoveFileExW(
			temporaryPath.c_str(),
			storagePath.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Camera Bookmarks: atomic storage replacement failed.");
		}
	}
}

CameraBookmarkStore::CityBookmarks* CameraBookmarkStore::ActiveCityLocked()
{
	if (activeCityKey.empty()) {
		return nullptr;
	}
	for (CityBookmarks& city : cities) {
		if (city.key == activeCityKey) {
			return &city;
		}
	}
	return nullptr;
}

const CameraBookmarkStore::CityBookmarks* CameraBookmarkStore::ActiveCityLocked() const
{
	if (activeCityKey.empty()) {
		return nullptr;
	}
	for (const CityBookmarks& city : cities) {
		if (city.key == activeCityKey) {
			return &city;
		}
	}
	return nullptr;
}

bool CameraBookmarkStore::IsSlotValid(uint32_t slot)
{
	return slot < kSlotCount;
}
