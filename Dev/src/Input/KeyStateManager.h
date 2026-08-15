#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

enum class KeyOwner : uint8_t
{
	None,
	ModernCameraPan,
	ModernCameraRotation,
	ModernCameraClassicAngle,
};

struct TrackedKeyState
{
	bool down = false;
	KeyOwner owner = KeyOwner::None;
};

class KeyStateManager
{
public:
	using PhysicalKeyStateReader = bool(*)(int key);

	explicit KeyStateManager(PhysicalKeyStateReader physicalKeyStateReader);

	bool IsTrackable(WPARAM key) const;
	bool IsPhysicalDown(int key) const;
	void Observe(WPARAM key, bool down);
	bool IsDown(int key) const;
	bool IsDownWithFallback(int key) const;
	void RefreshModifiers();
	bool Claim(WPARAM key, KeyOwner owner);
	void Release(WPARAM key, KeyOwner owner);
	bool IsOwnedBy(WPARAM key, KeyOwner owner) const;
	KeyOwner GetOwner(WPARAM key) const;
	void Clear();

	template <size_t Count>
	bool AnyDownOwnedBy(const std::array<WPARAM, Count>& keys, KeyOwner owner) const
	{
		return AnyDownOwnedBy(keys.data(), Count, owner);
	}

	template <size_t Count>
	bool AnyOwnedBy(const std::array<WPARAM, Count>& keys, KeyOwner owner) const
	{
		return AnyOwnedBy(keys.data(), Count, owner);
	}

	template <size_t Count>
	void ReleaseAll(const std::array<WPARAM, Count>& keys, KeyOwner owner)
	{
		ReleaseAll(keys.data(), Count, owner);
	}

private:
	bool AnyDownOwnedBy(const WPARAM* keys, size_t count, KeyOwner owner) const;
	bool AnyOwnedBy(const WPARAM* keys, size_t count, KeyOwner owner) const;
	void ReleaseAll(const WPARAM* keys, size_t count, KeyOwner owner);

	PhysicalKeyStateReader mPhysicalKeyStateReader;
	std::array<TrackedKeyState, 256> mKeyStates{};
};
