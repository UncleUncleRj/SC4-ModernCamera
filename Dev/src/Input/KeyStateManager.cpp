#include "KeyStateManager.h"

KeyStateManager::KeyStateManager(PhysicalKeyStateReader physicalKeyStateReader)
	: mPhysicalKeyStateReader(physicalKeyStateReader)
{
}

bool KeyStateManager::IsTrackable(WPARAM key) const
{
	return key < mKeyStates.size();
}

bool KeyStateManager::IsPhysicalDown(int key) const
{
	return mPhysicalKeyStateReader && mPhysicalKeyStateReader(key);
}

void KeyStateManager::Observe(WPARAM key, bool down)
{
	if (!IsTrackable(key)) {
		return;
	}

	mKeyStates[key].down = down;
	switch (key) {
	case VK_LSHIFT:
	case VK_RSHIFT:
		mKeyStates[VK_SHIFT].down = down
			|| IsPhysicalDown(VK_LSHIFT)
			|| IsPhysicalDown(VK_RSHIFT);
		break;
	case VK_LCONTROL:
	case VK_RCONTROL:
		mKeyStates[VK_CONTROL].down = down
			|| IsPhysicalDown(VK_LCONTROL)
			|| IsPhysicalDown(VK_RCONTROL);
		break;
	case VK_LMENU:
	case VK_RMENU:
		mKeyStates[VK_MENU].down = down
			|| IsPhysicalDown(VK_LMENU)
			|| IsPhysicalDown(VK_RMENU);
		break;
	default:
		break;
	}
}

bool KeyStateManager::IsDown(int key) const
{
	return key >= 0
		&& key < static_cast<int>(mKeyStates.size())
		&& mKeyStates[static_cast<size_t>(key)].down;
}

bool KeyStateManager::IsDownWithFallback(int key) const
{
	return IsDown(key) || IsPhysicalDown(key);
}

void KeyStateManager::RefreshModifiers()
{
	mKeyStates[VK_SHIFT].down = IsPhysicalDown(VK_SHIFT)
		|| IsPhysicalDown(VK_LSHIFT)
		|| IsPhysicalDown(VK_RSHIFT);
	mKeyStates[VK_CONTROL].down = IsPhysicalDown(VK_CONTROL)
		|| IsPhysicalDown(VK_LCONTROL)
		|| IsPhysicalDown(VK_RCONTROL);
	mKeyStates[VK_MENU].down = IsPhysicalDown(VK_MENU)
		|| IsPhysicalDown(VK_LMENU)
		|| IsPhysicalDown(VK_RMENU);
}

bool KeyStateManager::Claim(WPARAM key, KeyOwner owner)
{
	if (!IsTrackable(key)) {
		return false;
	}

	TrackedKeyState& state = mKeyStates[key];
	if (state.owner != KeyOwner::None && state.owner != owner) {
		return false;
	}

	state.owner = owner;
	return true;
}

void KeyStateManager::Release(WPARAM key, KeyOwner owner)
{
	if (!IsTrackable(key)) {
		return;
	}

	TrackedKeyState& state = mKeyStates[key];
	if (state.owner == owner) {
		state.owner = KeyOwner::None;
	}
}

bool KeyStateManager::IsOwnedBy(WPARAM key, KeyOwner owner) const
{
	return IsTrackable(key) && mKeyStates[key].owner == owner;
}

KeyOwner KeyStateManager::GetOwner(WPARAM key) const
{
	return IsTrackable(key) ? mKeyStates[key].owner : KeyOwner::None;
}

void KeyStateManager::Clear()
{
	for (TrackedKeyState& state : mKeyStates) {
		state.down = false;
		state.owner = KeyOwner::None;
	}
}

bool KeyStateManager::AnyDownOwnedBy(const WPARAM* keys, size_t count, KeyOwner owner) const
{
	for (size_t index = 0; index < count; ++index) {
		const WPARAM key = keys[index];
		if (IsOwnedBy(key, owner) && IsDown(static_cast<int>(key))) {
			return true;
		}
	}

	return false;
}

bool KeyStateManager::AnyOwnedBy(const WPARAM* keys, size_t count, KeyOwner owner) const
{
	for (size_t index = 0; index < count; ++index) {
		if (IsOwnedBy(keys[index], owner)) {
			return true;
		}
	}

	return false;
}

void KeyStateManager::ReleaseAll(const WPARAM* keys, size_t count, KeyOwner owner)
{
	for (size_t index = 0; index < count; ++index) {
		Release(keys[index], owner);
	}
}
