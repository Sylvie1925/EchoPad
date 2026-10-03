#include "devices.h"

#include <mmdeviceapi.h>
#include <propidl.h>

#include "core/util.h"

namespace echopad {
namespace {

// {a45c254e-df1c-4efd-8020-67d146a850e0}, 14  (PKEY_Device_FriendlyName)
const PROPERTYKEY kFriendlyName = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

EDataFlow ToDataFlow(DeviceFlow flow) {
    return flow == DeviceFlow::Render ? eRender : eCapture;
}

ComPtr<IMMDeviceEnumerator> CreateEnumerator() {
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                IID_IMMDeviceEnumerator, enumerator.PutVoid()))) {
        return ComPtr<IMMDeviceEnumerator>();
    }
    return enumerator;
}

std::wstring FriendlyName(IMMDevice* device) {
    ComPtr<IPropertyStore> store;
    if (FAILED(device->OpenPropertyStore(STGM_READ, store.Put()))) {
        return L"";
    }
    PROPVARIANT value;
    PropVariantInit(&value);
    std::wstring name;
    if (SUCCEEDED(store->GetValue(kFriendlyName, &value)) && value.vt == VT_LPWSTR &&
        value.pwszVal) {
        name = value.pwszVal;
    }
    PropVariantClear(&value);
    return name;
}

}  // namespace

std::vector<DeviceInfo> EnumerateDevices(DeviceFlow flow) {
    std::vector<DeviceInfo> devices;

    ComPtr<IMMDeviceEnumerator> enumerator = CreateEnumerator();
    if (!enumerator) {
        return devices;
    }

    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator->EnumAudioEndpoints(ToDataFlow(flow), DEVICE_STATE_ACTIVE,
                                              collection.Put()))) {
        return devices;
    }

    UINT count = 0;
    if (FAILED(collection->GetCount(&count))) {
        return devices;
    }

    const std::wstring defaultId = DefaultDeviceId(flow);

    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(index, device.Put()))) {
            continue;
        }
        LPWSTR rawId = nullptr;
        if (FAILED(device->GetId(&rawId)) || !rawId) {
            continue;
        }
        DeviceInfo info;
        info.id = rawId;
        CoTaskMemFree(rawId);

        info.name = FriendlyName(device.Get());
        if (info.name.empty()) {
            info.name = L"(unnamed audio device)";
        }
        info.isDefault = !defaultId.empty() && info.id == defaultId;
        devices.push_back(std::move(info));
    }

    return devices;
}

std::wstring DefaultDeviceId(DeviceFlow flow) {
    ComPtr<IMMDeviceEnumerator> enumerator = CreateEnumerator();
    if (!enumerator) {
        return L"";
    }
    ComPtr<IMMDevice> device;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(ToDataFlow(flow), eConsole, device.Put()))) {
        return L"";
    }
    LPWSTR rawId = nullptr;
    if (FAILED(device->GetId(&rawId)) || !rawId) {
        return L"";
    }
    std::wstring id = rawId;
    CoTaskMemFree(rawId);
    return id;
}

std::wstring ResolveDeviceId(DeviceFlow flow, const std::wstring& preferredId) {
    const std::vector<DeviceInfo> devices = EnumerateDevices(flow);

    if (!preferredId.empty()) {
        for (const DeviceInfo& device : devices) {
            if (device.id == preferredId) {
                return device.id;
            }
        }
    }
    const std::wstring defaultId = DefaultDeviceId(flow);
    if (!defaultId.empty()) {
        return defaultId;
    }
    return devices.empty() ? std::wstring() : devices.front().id;
}

bool LooksLikeVirtualCable(const std::wstring& friendlyName) {
    static const wchar_t* kMarkers[] = {
        L"cable input", L"cable output", L"vb-audio", L"voicemeeter",
        L"virtual audio", L"virtual cable", L"vac ", L"vb-cable",
    };
    const std::wstring lowered = ToLower(friendlyName);
    for (const wchar_t* marker : kMarkers) {
        if (lowered.find(marker) != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

std::wstring FindVirtualCableId(DeviceFlow flow) {
    const std::vector<DeviceInfo> devices = EnumerateDevices(flow);
    for (const DeviceInfo& device : devices) {
        if (LooksLikeVirtualCable(device.name)) {
            return device.id;
        }
    }
    return L"";
}

}  // namespace echopad
