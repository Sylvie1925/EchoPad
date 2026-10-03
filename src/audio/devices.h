// EchoPad - audio endpoint enumeration.
#pragma once

#include "common.h"

namespace echopad {

enum class DeviceFlow { Render, Capture };

struct DeviceInfo {
    std::wstring id;    // endpoint ID; stable across restarts and re-plugs
    std::wstring name;  // friendly name, e.g. "CABLE Input (VB-Audio Virtual Cable)"
    bool isDefault = false;
};

// Active endpoints for the given flow, in the order Windows reports them.
std::vector<DeviceInfo> EnumerateDevices(DeviceFlow flow);

// Endpoint ID of the current default device, or an empty string.
std::wstring DefaultDeviceId(DeviceFlow flow);

// Returns `preferredId` when it still exists, otherwise the default device's ID,
// otherwise the first endpoint found. Empty when no device is available at all.
std::wstring ResolveDeviceId(DeviceFlow flow, const std::wstring& preferredId);

// Heuristic used only to preselect a device and to show a setup hint. Never
// changes routing on its own.
bool LooksLikeVirtualCable(const std::wstring& friendlyName);

// Picks the endpoint ID most likely to be a virtual cable on the given flow.
std::wstring FindVirtualCableId(DeviceFlow flow);

}  // namespace echopad
