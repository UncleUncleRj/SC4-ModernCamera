#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

enum class KeyOwner : uint8_t
{
	// The key is not claimed by a Modern Camera feature.
	None,
	// The key drives keyboard panning while it remains held.
	ModernCameraPan,
	// The middle mouse button or Q and E own an active free-camera rotation gesture.
	ModernCameraRotation,
	// Alt+Q or Alt+E owns a native Classic camera rotation step.
	ModernCameraClassicRotation,
	// An Alt key owns the temporary Classic-angle hold.
	ModernCameraClassicAngle,
};

// Observed state and Modern Camera claim for one virtual key.
struct TrackedKeyState
{
	// Last observed down state for this virtual key.
	bool down = false;
	// Modern Camera feature that currently owns the key.
	KeyOwner owner = KeyOwner::None;
};

/// <summary>
/// Returns the button GetAsyncKeyState must read for a mouse button. It reads physical
/// buttons, so when the player has swapped them the logical right button is the physical
/// left one, and the reverse.
/// </summary>
/// <param name="key">Virtual key, which may be a mouse button.</param>
/// <param name="buttonsSwapped">Whether the primary and secondary buttons are swapped (SM_SWAPBUTTON).</param>
/// <returns>The physical button for VK_LBUTTON or VK_RBUTTON; any other key unchanged.</returns>
inline int GetPhysicalMouseButtonKey(int key, bool buttonsSwapped)
{
	if (!buttonsSwapped) {
		return key;
	}
	if (key == VK_LBUTTON) {
		return VK_RBUTTON;
	}
	if (key == VK_RBUTTON) {
		return VK_LBUTTON;
	}
	return key;
}

/// <summary>
/// Returns whether a queued key message is newer than the last event the low-level keyboard
/// hook saw. The hook runs for every key event before its message is queued, and
/// KBDLLHOOKSTRUCT::time equals the message's GetMessageTime, so a newer message means
/// Windows removed the hook. It does that silently after the game thread stalls past the
/// hook timeout.
/// </summary>
/// <param name="messageTime">GetMessageTime of the key message being handled.</param>
/// <param name="lastHookEventTime">Time of the last event the hook saw.</param>
/// <returns>True when the message is newer, measured across the tick counter's wrap.</returns>
inline bool IsKeyboardMessageAheadOfHook(DWORD messageTime, DWORD lastHookEventTime)
{
	return static_cast<LONG>(messageTime - lastHookEventTime) > 0;
}

// Tracks observed virtual-key state and Modern Camera ownership claims.
class KeyStateManager
{
public:
	// Reads a physical virtual-key state for fallback and modifier refreshes.
	using PhysicalKeyStateReader = bool(*)(int key);

	// Creates an empty tracker that reads physical state through the supplied callback.
	explicit KeyStateManager(PhysicalKeyStateReader physicalKeyStateReader);

	// Returns whether the key fits in the fixed virtual-key state table.
	bool IsTrackable(WPARAM key) const;
	// Returns the physical state supplied by the reader callback.
	bool IsPhysicalDown(int key) const;
	// Records a key transition and refreshes the aggregate modifier state when needed.
	void Observe(WPARAM key, bool down);
	// Returns the last observed state for a valid virtual key.
	bool IsDown(int key) const;
	// Returns the observed state or the physical state when an event was missed.
	bool IsDownWithFallback(int key) const;
	// Refreshes Shift, Ctrl, and Alt from physical key state.
	void RefreshModifiers();
	// Claims an unclaimed key for a feature and returns false on conflicting ownership.
	bool Claim(WPARAM key, KeyOwner owner);
	// Releases a key only when the supplied feature owns it.
	void Release(WPARAM key, KeyOwner owner);
	// Returns whether the supplied feature owns the key.
	bool IsOwnedBy(WPARAM key, KeyOwner owner) const;
	// Returns the current owner or None when the key is not trackable.
	KeyOwner GetOwner(WPARAM key) const;
	// Clears every observed state and ownership claim.
	void Clear();
	/// <summary>
	/// Clears any mouse button the tracker holds down that a mouse move reports as up. A
	/// release the tracker missed, such as one over a plugin window, cannot then leave the
	/// button down. A move never records a press.
	/// </summary>
	/// <param name="mouseKeyFlags">The MK_ button flags of a WM_MOUSEMOVE message.</param>
	void ObserveMouseMoveButtons(WPARAM mouseKeyFlags);

	// Returns whether an owned key in the supplied fixed group is still down.
	template <size_t Count>
	bool AnyDownOwnedBy(const std::array<WPARAM, Count>& keys, KeyOwner owner) const
	{
		return AnyDownOwnedBy(keys.data(), Count, owner);
	}

	// Returns whether the supplied feature owns any key in the fixed group.
	template <size_t Count>
	bool AnyOwnedBy(const std::array<WPARAM, Count>& keys, KeyOwner owner) const
	{
		return AnyOwnedBy(keys.data(), Count, owner);
	}

	// Releases the supplied feature's ownership of every key in the fixed group.
	template <size_t Count>
	void ReleaseAll(const std::array<WPARAM, Count>& keys, KeyOwner owner)
	{
		ReleaseAll(keys.data(), Count, owner);
	}

private:
	// Returns whether an owned key in the group is still down.
	bool AnyDownOwnedBy(const WPARAM* keys, size_t count, KeyOwner owner) const;
	// Returns whether the supplied feature owns any key in the group.
	bool AnyOwnedBy(const WPARAM* keys, size_t count, KeyOwner owner) const;
	// Releases the supplied feature's claims in the key group.
	void ReleaseAll(const WPARAM* keys, size_t count, KeyOwner owner);

	// Callback used to recover state when a Windows input transition was missed.
	PhysicalKeyStateReader mPhysicalKeyStateReader;
	// Per-virtual-key observed state and Modern Camera ownership.
	std::array<TrackedKeyState, 256> mKeyStates{};
};
