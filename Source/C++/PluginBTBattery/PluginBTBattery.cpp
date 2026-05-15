#include <Windows.h>
#include <SetupAPI.h>
#include <cfgmgr32.h>
#include <devpropdef.h>
#include <hidsdi.h>

#include <algorithm>
#include <cwctype>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../API/RainmeterAPI.h"

#pragma comment(lib, "Setupapi.lib")
#pragma comment(lib, "Cfgmgr32.lib")
#pragma comment(lib, "Hid.lib")

namespace
{
    const DEVPROPKEY PKEY_BluetoothBatteryLevel = { { 0x104ea319, 0x6ee2, 0x4701, { 0xbd, 0x47, 0x8d, 0xdb, 0xf4, 0x25, 0xbb, 0xe5 } }, 2 };
    const DEVPROPKEY PKEY_DeviceIsConnected = { { 0x83da6326, 0x97a6, 0x4088, { 0x94, 0x53, 0xa1, 0x92, 0x3f, 0x57, 0x3b, 0x29 } }, 15 };
    const DEVPROPKEY PKEY_BluetoothDeviceAddress = { { 0x2bd67d8b, 0x8beb, 0x48d5, { 0x87, 0xe0, 0x6c, 0xda, 0x34, 0x28, 0x04, 0x0a } }, 1 };

    struct VidPid
    {
        USHORT vendorId = 0;
        USHORT productId = 0;
        bool valid = false;
    };

    struct DeviceEntry
    {
        std::wstring instanceId;
        std::wstring friendlyName;
        std::wstring bluetoothAddress;
        VidPid vidPid;
        bool hasConnected = false;
        bool connected = false;
        bool hasBattery = false;
        int battery = -1;
    };

    struct HidBatteryEntry
    {
        std::wstring devicePath;
        VidPid vidPid;
        int battery = -1;
        bool valid = false;
    };

    struct QueryResult
    {
        int value = -1;
        bool found = false;
        bool connected = false;
        bool batteryFound = false;
        bool hidBatteryFound = false;
        int candidates = 0;
        ULONGLONG snapshotTick = 0;
    };

    struct SnapshotCopy
    {
        std::vector<DeviceEntry> devices;
        std::vector<HidBatteryEntry> hidBatteries;
        ULONGLONG tick = 0;
        bool hasSnapshot = false;
    };

    struct SnapshotData
    {
        std::vector<DeviceEntry> devices;
        std::vector<HidBatteryEntry> hidBatteries;
    };

    struct Measure
    {
        void* rm = nullptr;
        std::wstring deviceName;
        std::wstring deviceAddress;
        bool allowContainsMatch = false;
        bool logging = false;
        int pollSeconds = 30;
        bool hasValue = false;
        ULONGLONG lastSnapshotTick = 0;
        double value = -1.0;
    };

    std::mutex g_snapshotMutex;
    SnapshotData g_snapshot;
    ULONGLONG g_snapshotTick = 0;
    bool g_hasSnapshot = false;
    bool g_scanInProgress = false;

    std::wstring ToUpper(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towupper(ch)); });
        return value;
    }

    bool ContainsInsensitive(const std::wstring& haystack, const std::wstring& needle)
    {
        if (needle.empty())
        {
            return false;
        }

        return ToUpper(haystack).find(ToUpper(needle)) != std::wstring::npos;
    }

    bool EqualsInsensitive(const std::wstring& left, const std::wstring& right)
    {
        return ToUpper(left) == ToUpper(right);
    }

    bool IsHex(wchar_t ch)
    {
        return (ch >= L'0' && ch <= L'9') || (ch >= L'A' && ch <= L'F') || (ch >= L'a' && ch <= L'f');
    }

    int HexValue(wchar_t ch)
    {
        if (ch >= L'0' && ch <= L'9') return ch - L'0';
        if (ch >= L'A' && ch <= L'F') return 10 + ch - L'A';
        if (ch >= L'a' && ch <= L'f') return 10 + ch - L'a';
        return -1;
    }

    bool TryParseHexWord(const std::wstring& text, size_t start, size_t length, USHORT& value)
    {
        if (start + length > text.size())
        {
            return false;
        }

        unsigned int parsed = 0;
        for (size_t i = start; i < start + length; ++i)
        {
            int nibble = HexValue(text[i]);
            if (nibble < 0)
            {
                return false;
            }
            parsed = (parsed << 4) | static_cast<unsigned int>(nibble);
        }

        value = static_cast<USHORT>(parsed & 0xFFFF);
        return true;
    }

    std::wstring NormalizeAddress(const std::wstring& address)
    {
        std::wstring normalized;
        for (wchar_t ch : address)
        {
            if (IsHex(ch))
            {
                normalized.push_back(static_cast<wchar_t>(std::towupper(ch)));
            }
        }
        return normalized;
    }

    void AddUnique(std::vector<std::wstring>& values, const std::wstring& value)
    {
        if (!value.empty() && std::find(values.begin(), values.end(), value) == values.end())
        {
            values.push_back(value);
        }
    }

    void AddUniqueVidPid(std::vector<VidPid>& values, const VidPid& value)
    {
        if (!value.valid)
        {
            return;
        }

        for (const VidPid& existing : values)
        {
            if (existing.valid && existing.vendorId == value.vendorId && existing.productId == value.productId)
            {
                return;
            }
        }

        values.push_back(value);
    }

    std::wstring ExtractAddressFromInstanceId(const std::wstring& instanceId)
    {
        std::wstring upper = ToUpper(instanceId);
        size_t pos = upper.find(L"DEV_");
        if (pos == std::wstring::npos)
        {
            return L"";
        }

        pos += 4;
        if (pos + 12 > upper.size())
        {
            return L"";
        }

        std::wstring address = upper.substr(pos, 12);
        for (wchar_t ch : address)
        {
            if (!IsHex(ch))
            {
                return L"";
            }
        }

        return address;
    }

    bool TryFindHexAfterMarker(const std::wstring& text, const std::wstring& marker, USHORT& value)
    {
        size_t pos = text.find(marker);
        if (pos == std::wstring::npos)
        {
            return false;
        }

        pos += marker.size();
        if (pos < text.size() && (text[pos] == L'_' || text[pos] == L'&'))
        {
            ++pos;
        }

        size_t hexStart = pos;
        size_t hexLength = 0;
        while (pos < text.size() && IsHex(text[pos]))
        {
            ++pos;
            ++hexLength;
        }

        if (hexLength >= 4)
        {
            return TryParseHexWord(text, hexStart + hexLength - 4, 4, value);
        }

        return false;
    }

    VidPid ExtractVidPid(const std::wstring& text)
    {
        std::wstring upper = ToUpper(text);
        VidPid result;
        result.valid = TryFindHexAfterMarker(upper, L"VID", result.vendorId) && TryFindHexAfterMarker(upper, L"PID", result.productId);
        return result;
    }

    bool SameVidPid(const VidPid& left, const VidPid& right)
    {
        return left.valid && right.valid && left.vendorId == right.vendorId && left.productId == right.productId;
    }
    bool HidProfileNameMatches(const VidPid& vidPid, const std::wstring& deviceName)
    {
        if (!vidPid.valid)
        {
            return false;
        }

        if (vidPid.vendorId == 0x054C && (vidPid.productId == 0x0CE6 || vidPid.productId == 0x0DF2))
        {
            return ContainsInsensitive(deviceName, L"DUALSENSE") || ContainsInsensitive(deviceName, L"WIRELESS CONTROLLER");
        }

        return false;
    }

    bool IsKnownHidBatteryProfile(const VidPid& vidPid)
    {
        if (!vidPid.valid)
        {
            return false;
        }

        // Sony DualSense USB and Bluetooth report layouts. More profiles can be added here without changing the public skin API.
        return vidPid.vendorId == 0x054C && (vidPid.productId == 0x0CE6 || vidPid.productId == 0x0DF2);
    }

    std::wstring GetRegistryStringProperty(HDEVINFO devices, SP_DEVINFO_DATA& info, DWORD property)
    {
        WCHAR buffer[512]{};
        DWORD dataType = 0;
        DWORD requiredSize = 0;
        if (SetupDiGetDeviceRegistryPropertyW(devices, &info, property, &dataType, reinterpret_cast<PBYTE>(buffer), sizeof(buffer), &requiredSize))
        {
            return buffer;
        }

        return L"";
    }

    std::wstring GetDeviceStringProperty(HDEVINFO devices, SP_DEVINFO_DATA& info, const DEVPROPKEY& key)
    {
        WCHAR buffer[512]{};
        DEVPROPTYPE propertyType = 0;
        DWORD requiredSize = 0;
        if (SetupDiGetDevicePropertyW(devices, &info, &key, &propertyType, reinterpret_cast<PBYTE>(buffer), sizeof(buffer), &requiredSize, 0) &&
            propertyType == DEVPROP_TYPE_STRING)
        {
            return buffer;
        }

        return L"";
    }

    bool TryReadIntProperty(HDEVINFO devices, SP_DEVINFO_DATA& info, const DEVPROPKEY& key, int& value)
    {
        BYTE buffer[64]{};
        DEVPROPTYPE propertyType = 0;
        DWORD requiredSize = 0;
        if (!SetupDiGetDevicePropertyW(devices, &info, &key, &propertyType, buffer, sizeof(buffer), &requiredSize, 0))
        {
            return false;
        }

        switch (propertyType)
        {
        case DEVPROP_TYPE_BYTE:
        case DEVPROP_TYPE_BOOLEAN:
            value = static_cast<int>(buffer[0]);
            return true;
        case DEVPROP_TYPE_INT16:
            value = static_cast<int>(*reinterpret_cast<SHORT*>(buffer));
            return true;
        case DEVPROP_TYPE_UINT16:
            value = static_cast<int>(*reinterpret_cast<USHORT*>(buffer));
            return true;
        case DEVPROP_TYPE_INT32:
            value = static_cast<int>(*reinterpret_cast<LONG*>(buffer));
            return true;
        case DEVPROP_TYPE_UINT32:
            value = static_cast<int>(*reinterpret_cast<ULONG*>(buffer));
            return true;
        case DEVPROP_TYPE_INT64:
            value = static_cast<int>(*reinterpret_cast<LONGLONG*>(buffer));
            return true;
        case DEVPROP_TYPE_UINT64:
            value = static_cast<int>(*reinterpret_cast<ULONGLONG*>(buffer));
            return true;
        default:
            return false;
        }
    }

    bool NameMatches(const std::wstring& actualName, const std::wstring& expectedName, bool allowContainsMatch)
    {
        if (expectedName.empty())
        {
            return false;
        }

        return EqualsInsensitive(actualName, expectedName) || (allowContainsMatch && ContainsInsensitive(actualName, expectedName));
    }

    bool AddressMatches(const std::wstring& instanceId, const std::vector<std::wstring>& addresses)
    {
        std::wstring upperInstance = ToUpper(instanceId);
        for (const std::wstring& address : addresses)
        {
            if (!address.empty() && upperInstance.find(address) != std::wstring::npos)
            {
                return true;
            }
        }

        return false;
    }

    void TryWakeHidProfile(const std::wstring& path, const VidPid& vidPid)
    {
        if (!IsKnownHidBatteryProfile(vidPid))
        {
            return;
        }

        HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            return;
        }

        BYTE feature[41]{};
        feature[0] = 0x05;
        HidD_GetFeature(handle, feature, sizeof(feature));
        CloseHandle(handle);
    }
    bool ReadHidInputReport(const std::wstring& path, std::vector<BYTE>& report, DWORD timeoutMs)
    {
        HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        USHORT inputLength = 78;
        PHIDP_PREPARSED_DATA preparsedData = nullptr;
        if (HidD_GetPreparsedData(handle, &preparsedData))
        {
            HIDP_CAPS caps{};
            if (HidP_GetCaps(preparsedData, &caps) == HIDP_STATUS_SUCCESS && caps.InputReportByteLength > 0)
            {
                inputLength = caps.InputReportByteLength;
            }
            HidD_FreePreparsedData(preparsedData);
        }

        report.assign(inputLength, 0);
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!overlapped.hEvent)
        {
            CloseHandle(handle);
            return false;
        }

        DWORD bytesRead = 0;
        BOOL ok = ReadFile(handle, report.data(), static_cast<DWORD>(report.size()), &bytesRead, &overlapped);
        if (!ok && GetLastError() == ERROR_IO_PENDING)
        {
            DWORD waitResult = WaitForSingleObject(overlapped.hEvent, timeoutMs);
            if (waitResult == WAIT_OBJECT_0)
            {
                ok = GetOverlappedResult(handle, &overlapped, &bytesRead, FALSE);
            }
            else
            {
                CancelIo(handle);
                ok = FALSE;
            }
        }

        CloseHandle(overlapped.hEvent);
        CloseHandle(handle);

        if (!ok || bytesRead == 0)
        {
            return false;
        }

        if (bytesRead < report.size())
        {
            report.resize(bytesRead);
        }

        return true;
    }

    bool TryParseKnownHidBatteryReport(const VidPid& vidPid, const std::vector<BYTE>& report, int& battery)
    {
        if (!IsKnownHidBatteryProfile(vidPid) || report.size() < 54)
        {
            return false;
        }

        int status = -1;
        if (report[0] == 0x31 && report.size() >= 55)
        {
            status = report[54];
        }
        else if (report[0] == 0x01 && report.size() == 64)
        {
            status = report[53];
        }
        else
        {
            return false;
        }

        int level = status & 0x0F;
        int charging = (status & 0xF0) >> 4;
        if (charging == 0x02)
        {
            battery = 100;
        }
        else if (charging == 0x00 || charging == 0x01)
        {
            battery = min((level * 10) + 5, 100);
        }
        else
        {
            battery = 0;
        }

        return true;
    }

    std::vector<std::wstring> EnumerateHidPaths()
    {
        std::vector<std::wstring> paths;
        GUID hidGuid{};
        HidD_GetHidGuid(&hidGuid);

        HDEVINFO devices = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (devices == INVALID_HANDLE_VALUE)
        {
            return paths;
        }

        for (DWORD index = 0;; ++index)
        {
            SP_DEVICE_INTERFACE_DATA data{};
            data.cbSize = sizeof(data);
            if (!SetupDiEnumDeviceInterfaces(devices, nullptr, &hidGuid, index, &data))
            {
                break;
            }

            DWORD requiredSize = 0;
            SetupDiGetDeviceInterfaceDetailW(devices, &data, nullptr, 0, &requiredSize, nullptr);
            if (requiredSize == 0)
            {
                continue;
            }

            std::vector<BYTE> detailBuffer(requiredSize);
            auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuffer.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (SetupDiGetDeviceInterfaceDetailW(devices, &data, detail, requiredSize, nullptr, nullptr))
            {
                paths.push_back(detail->DevicePath);
            }
        }

        SetupDiDestroyDeviceInfoList(devices);
        return paths;
    }

    std::vector<HidBatteryEntry> BuildHidBatterySnapshot()
    {
        std::vector<HidBatteryEntry> entries;
        for (const std::wstring& path : EnumerateHidPaths())
        {
            VidPid vidPid = ExtractVidPid(path);
            if (!IsKnownHidBatteryProfile(vidPid))
            {
                continue;
            }

            TryWakeHidProfile(path, vidPid);
            for (int attempt = 0; attempt < 8; ++attempt)
            {
                std::vector<BYTE> report;
                int battery = -1;
                if (ReadHidInputReport(path, report, 700) && TryParseKnownHidBatteryReport(vidPid, report, battery))
                {
                    HidBatteryEntry entry;
                    entry.devicePath = path;
                    entry.vidPid = vidPid;
                    entry.battery = battery;
                    entry.valid = true;
                    entries.push_back(entry);
                    break;
                }
            }
        }

        return entries;
    }

    SnapshotData BuildDeviceSnapshot()
    {
        SnapshotData snapshot;
        HDEVINFO devices = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES);
        if (devices != INVALID_HANDLE_VALUE)
        {
            for (DWORD index = 0;; ++index)
            {
                SP_DEVINFO_DATA info{};
                info.cbSize = sizeof(info);
                if (!SetupDiEnumDeviceInfo(devices, index, &info))
                {
                    break;
                }

                WCHAR instanceBuffer[MAX_DEVICE_ID_LEN]{};
                if (CM_Get_Device_IDW(info.DevInst, instanceBuffer, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS)
                {
                    continue;
                }

                DeviceEntry entry;
                entry.instanceId = instanceBuffer;
                entry.friendlyName = GetRegistryStringProperty(devices, info, SPDRP_FRIENDLYNAME);
                if (entry.friendlyName.empty())
                {
                    entry.friendlyName = GetRegistryStringProperty(devices, info, SPDRP_DEVICEDESC);
                }

                entry.bluetoothAddress = NormalizeAddress(GetDeviceStringProperty(devices, info, PKEY_BluetoothDeviceAddress));
                entry.vidPid = ExtractVidPid(entry.instanceId);

                int connectedValue = 0;
                if (TryReadIntProperty(devices, info, PKEY_DeviceIsConnected, connectedValue))
                {
                    entry.hasConnected = true;
                    entry.connected = connectedValue != 0;
                }

                int batteryValue = 0;
                if (TryReadIntProperty(devices, info, PKEY_BluetoothBatteryLevel, batteryValue))
                {
                    entry.hasBattery = true;
                    if (batteryValue >= 0 && batteryValue <= 100)
                    {
                        entry.battery = batteryValue;
                    }
                }

                snapshot.devices.push_back(entry);
            }

            SetupDiDestroyDeviceInfoList(devices);
        }

        snapshot.hidBatteries = BuildHidBatterySnapshot();
        return snapshot;
    }

    void RefreshSnapshotWorker()
    {
        SnapshotData snapshot = BuildDeviceSnapshot();
        std::lock_guard<std::mutex> lock(g_snapshotMutex);
        g_snapshot = std::move(snapshot);
        g_snapshotTick = GetTickCount64();
        g_hasSnapshot = true;
        g_scanInProgress = false;
    }

    SnapshotCopy GetSnapshotCopyAndQueueRefresh(int pollSeconds)
    {
        bool startWorker = false;
        SnapshotCopy copy;
        ULONGLONG now = GetTickCount64();

        {
            std::lock_guard<std::mutex> lock(g_snapshotMutex);
            const ULONGLONG interval = static_cast<ULONGLONG>(pollSeconds) * 1000ULL;
            const bool stale = !g_hasSnapshot || (now - g_snapshotTick >= interval);
            if (stale && !g_scanInProgress)
            {
                g_scanInProgress = true;
                startWorker = true;
            }

            copy.devices = g_snapshot.devices;
            copy.hidBatteries = g_snapshot.hidBatteries;
            copy.tick = g_snapshotTick;
            copy.hasSnapshot = g_hasSnapshot;
        }

        if (startWorker)
        {
            std::thread(RefreshSnapshotWorker).detach();
        }

        return copy;
    }

    QueryResult QueryBluetoothBattery(const Measure* measure, const SnapshotCopy& snapshot)
    {
        QueryResult result;
        result.snapshotTick = snapshot.tick;
        if (!snapshot.hasSnapshot)
        {
            return result;
        }

        std::vector<std::wstring> addresses;
        std::vector<VidPid> vidPids;
        AddUnique(addresses, NormalizeAddress(measure->deviceAddress));

        std::vector<size_t> rootIndexes;
        for (size_t i = 0; i < snapshot.devices.size(); ++i)
        {
            const DeviceEntry& entry = snapshot.devices[i];
            bool rootMatch = false;

            if (!addresses.empty() && AddressMatches(entry.instanceId, addresses) &&
                (ContainsInsensitive(entry.instanceId, L"BTHENUM\\DEV_") || ContainsInsensitive(entry.instanceId, L"BTHLE\\DEV_")))
            {
                rootMatch = true;
            }

            if (NameMatches(entry.friendlyName, measure->deviceName, measure->allowContainsMatch))
            {
                rootMatch = true;
            }

            if (rootMatch)
            {
                rootIndexes.push_back(i);
                AddUnique(addresses, ExtractAddressFromInstanceId(entry.instanceId));
                AddUnique(addresses, entry.bluetoothAddress);
                AddUniqueVidPid(vidPids, entry.vidPid);
            }
        }

        result.found = !rootIndexes.empty();
        if (!result.found)
        {
            return result;
        }

        for (size_t i = 0; i < snapshot.devices.size(); ++i)
        {
            const DeviceEntry& entry = snapshot.devices[i];
            bool isRoot = std::find(rootIndexes.begin(), rootIndexes.end(), i) != rootIndexes.end();
            bool isCandidate = isRoot || AddressMatches(entry.instanceId, addresses) || NameMatches(entry.friendlyName, measure->deviceName, measure->allowContainsMatch);
            if (!isCandidate)
            {
                continue;
            }

            ++result.candidates;
            AddUniqueVidPid(vidPids, entry.vidPid);

            if (entry.hasConnected && entry.connected)
            {
                result.connected = true;
            }

            if (entry.hasBattery)
            {
                result.batteryFound = true;
                if (entry.battery >= 0 && entry.battery <= 100)
                {
                    result.value = entry.battery;
                }
            }

            if (result.connected && result.value >= 0)
            {
                break;
            }
        }

        if (result.value < 0)
        {
            for (const HidBatteryEntry& hidBattery : snapshot.hidBatteries)
            {
                if (!hidBattery.valid)
                {
                    continue;
                }

                bool matchesKnownIdentity = false;
                for (const VidPid& vidPid : vidPids)
                {
                    if (SameVidPid(vidPid, hidBattery.vidPid))
                    {
                        matchesKnownIdentity = true;
                        break;
                    }
                }

                bool matchesProfileName = HidProfileNameMatches(hidBattery.vidPid, measure->deviceName);
                if (matchesProfileName && !matchesKnownIdentity)
                {
                    int matchingProfileCount = 0;
                    for (const HidBatteryEntry& candidate : snapshot.hidBatteries)
                    {
                        if (candidate.valid && HidProfileNameMatches(candidate.vidPid, measure->deviceName))
                        {
                            ++matchingProfileCount;
                        }
                    }
                    matchesProfileName = matchingProfileCount == 1;
                }

                if (matchesKnownIdentity || matchesProfileName)
                {
                    result.found = true;
                    result.value = hidBattery.battery;
                    result.connected = true;
                    result.batteryFound = true;
                    result.hidBatteryFound = true;
                    break;
                }
            }
        }

        if (!result.connected)
        {
            result.value = -1;
        }

        return result;
    }

    void LogResult(Measure* measure, const QueryResult& result)
    {
        if (!measure->logging)
        {
            return;
        }

        RmLogF(measure->rm, LOG_NOTICE,
            L"BTBattery: name='%s' address='%s' value=%d found=%d connected=%d batteryFound=%d hidBatteryFound=%d candidates=%d snapshot=%llu",
            measure->deviceName.c_str(), measure->deviceAddress.c_str(), result.value,
            result.found ? 1 : 0, result.connected ? 1 : 0, result.batteryFound ? 1 : 0,
            result.hidBatteryFound ? 1 : 0, result.candidates, result.snapshotTick);
    }
}

PLUGIN_EXPORT void Initialize(void** data, void* rm)
{
    Measure* measure = new Measure;
    measure->rm = rm;
    *data = measure;
}

PLUGIN_EXPORT void Reload(void* data, void* rm, double* maxValue)
{
    Measure* measure = static_cast<Measure*>(data);
    measure->rm = rm;
    measure->deviceName = RmReadString(rm, L"DeviceName", L"");
    measure->deviceAddress = NormalizeAddress(RmReadString(rm, L"DeviceAddress", L""));
    std::wstring matchMode = ToUpper(RmReadString(rm, L"MatchMode", L"Exact"));
    measure->allowContainsMatch = matchMode == L"CONTAINS" || matchMode == L"PARTIAL" || RmReadInt(rm, L"AllowContainsMatch", 0) != 0;
    measure->logging = RmReadInt(rm, L"EnableLogging", 0) != 0;
    measure->pollSeconds = RmReadInt(rm, L"PollSeconds", 30);
    if (measure->pollSeconds < 1)
    {
        measure->pollSeconds = 1;
    }

    measure->hasValue = false;
    measure->lastSnapshotTick = 0;
    measure->value = -1.0;
    *maxValue = 100.0;
}

PLUGIN_EXPORT double Update(void* data)
{
    Measure* measure = static_cast<Measure*>(data);
    SnapshotCopy snapshot = GetSnapshotCopyAndQueueRefresh(measure->pollSeconds);
    if (!snapshot.hasSnapshot)
    {
        return measure->hasValue ? measure->value : -1.0;
    }

    if (measure->hasValue && measure->lastSnapshotTick == snapshot.tick)
    {
        return measure->value;
    }

    QueryResult result = QueryBluetoothBattery(measure, snapshot);
    measure->value = static_cast<double>(result.value);
    measure->hasValue = true;
    measure->lastSnapshotTick = snapshot.tick;
    LogResult(measure, result);
    return measure->value;
}

PLUGIN_EXPORT void Finalize(void* data)
{
    Measure* measure = static_cast<Measure*>(data);
    delete measure;
}




