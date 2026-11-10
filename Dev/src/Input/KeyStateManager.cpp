#include "KeyStateManager.h"

// Stores the callback used when tracked state needs a physical-key fallback.
KeyStateManager::KeyStateManager(PhysicalKeyStateReader physicalKeyStateReader)
	: mPhysicalKeyStateReader(physicalKeyStateReader)
{
}

// Returns whether the virtual key is representable by the fixed state table.
bool KeyStateManager::IsTrackable(WPARAM key) const
{
	return key < mKeyStates.size();
}

// Queries the injected physical-key state reader when one is available.
bool KeyStateManager::IsPhysicalDown(int key) const
{
	return mPhysicalKeyStateReader && mPhysicalKeyStateReader(key);
}

// Records a transition and maintains aggregate Shift, Ctrl, and Alt state.
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

// Returns the last observed state for a valid virtual key.
bool KeyStateManager::IsDown(int key) const
{
	return key >= 0
		&& key < static_cast<int>(mKeyStates.size())
		&& mKeyStates[static_cast<size_t>(key)].down;
}

// Uses the physical state only when the observed state is not down.
bool KeyStateManager::IsDownWithFallback(int key) const
{
	return IsDown(key) || IsPhysicalDown(key);
}

// Rebuilds aggregate modifier state from the physical keyboard.
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

// Claims a key when no conflicting Modern Camera feature owns it.
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

// Releases a key only if the expected feature owns it.
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

// Reports whether the key has been claimed by the supplied feature.
bool KeyStateManager::IsOwnedBy(WPARAM key, KeyOwner owner) const
{
	return IsTrackable(key) && mKeyStates[key].owner == owner;
}

// Returns the key owner or None for an out-of-range key.
KeyOwner KeyStateManager::GetOwner(WPARAM key) const
{
	return IsTrackable(key) ? mKeyStates[key].owner : KeyOwner::None;
}

// Clears observed state and ownership for every virtual-key slot.
void KeyStateManager::Clear()
{
	for (TrackedKeyState& state : mKeyStates) {
		state.down = false;
		state.owner = KeyOwner::None;
	}
}

// Clears each tracked mouse button that the move reports as up; a move never records a press.
void KeyStateManager::ObserveMouseMoveButtons(WPARAM mouseKeyFlags)
{
	struct ButtonFlag
	{
		WPARAM key;
		WPARAM flag;
	};
	static constexpr ButtonFlag kButtons[] = {
		{ VK_LBUTTON, MK_LBUTTON },
		{ VK_RBUTTON, MK_RBUTTON },
		{ VK_MBUTTON, MK_MBUTTON },
	};
	for (const ButtonFlag& button : kButtons) {
		if ((mouseKeyFlags & button.flag) == 0) {
			mKeyStates[button.key].down = false;
		}
	}
}

// Returns whether an owned key in the supplied group is still held.
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

// Returns whether the supplied feature owns any key in the group.
bool KeyStateManager::AnyOwnedBy(const WPARAM* keys, size_t count, KeyOwner owner) const
{
	for (size_t index = 0; index < count; ++index) {
		if (IsOwnedBy(keys[index], owner)) {
			return true;
		}
	}

	return false;
}

// Releases the supplied feature's ownership of every key in the group.
void KeyStateManager::ReleaseAll(const WPARAM* keys, size_t count, KeyOwner owner)
{
	for (size_t index = 0; index < count; ++index) {
		Release(keys[index], owner);
	}
}
