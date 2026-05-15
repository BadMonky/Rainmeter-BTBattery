# BTBattery Rainmeter Plugin

BTBattery is a Rainmeter plugin for reading Bluetooth device battery levels.

Intended for wireless keyboards, mice, earbuds, controllers, and similar Bluetooth devices. The plugin first uses the normal Windows Bluetooth battery properties. For devices that do not expose battery level that way, it can use built-in HID report profiles when support has been added to the plugin.

## Release Layout

```text
Release/
  x64/BTBattery.dll
  x86/BTBattery.dll
Source/
  API/
  C++/PluginBTBattery/
  README.md
Examples/
  BTBatteryExample.ini
  README.md
```

## Manual Installation

1. Close Rainmeter.
2. Copy the x86 or x64 `BTBattery.dll` to:

```text
%APPDATA%\Rainmeter\Plugins\BTBattery.dll
```

3. Start Rainmeter.
4. Add a plugin measure to a skin and refresh it.

Use `Release\x64\BTBattery.dll` for 64-bit Rainmeter. Use `Release\x86\BTBattery.dll` for 32-bit Rainmeter.

## Basic Measure

```ini
[mBluetoothBattery]
Measure=Plugin
Plugin=BTBattery
DeviceName=Your Bluetooth Device Name
MatchMode=Exact
PollSeconds=30
EnableLogging=0
```

`DeviceName` should match the friendly name shown in Windows Bluetooth & devices settings.

## Measure Options

`DeviceName`  
Friendly device name to find. This is the normal setup option.

`DeviceAddress`  
Optional Bluetooth address override, written without separators, for example `001122AABBCC`. Leave this out unless two devices have the same or confusing names.

`MatchMode`  
`Exact` is recommended. `Contains` and `Partial` are available when Windows exposes a longer or awkward device name.

`AllowContainsMatch`  
Legacy boolean alternative to `MatchMode=Contains`. Prefer `MatchMode` in new skins.

`PollSeconds`  
How often the shared Bluetooth snapshot is refreshed in the background. The default is 30 seconds. Shorter values update faster but cost more system work.

`EnableLogging`  
Set to `1` to write diagnostic lines to the Rainmeter log. Keep this at `0` for normal use.

## Return Values

```text
-1      No reliable battery reading is available
0-100   Battery level percent
```

The first value after loading can briefly be `-1` while the background Bluetooth snapshot is built. This avoids blocking Rainmeter's UI thread.

For display meters, keep the raw plugin value and clamp only the visual value if desired:

```ini
[mBatteryDisplay]
Measure=Calc
Formula=Max([mBluetoothBattery], 0)
DynamicVariables=1
```

## Supported Devices

Most Bluetooth devices that expose the standard Windows battery property should work without a device-specific profile.

Built-in HID report profiles currently include:

- Sony DualSense Wireless Controller, USB VID `054C`, PID `0CE6` / `0DF2`

Devices that hide battery data in custom HID reports require a plugin update. You can request support by opening an issue with Rainmeter log output, the device name, Bluetooth address if known, and any HID report information you can capture.

## Building From Source

Open a Visual Studio Developer PowerShell or Developer Command Prompt, then run:

```powershell
cd Source\C++
msbuild .\PluginBTBattery\PluginBTBattery.vcxproj /p:Configuration=Release /p:Platform=x64
msbuild .\PluginBTBattery\PluginBTBattery.vcxproj /p:Configuration=Release /p:Platform=Win32
```

The source folder includes the minimum Rainmeter SDK API files needed by this project.

## License

MIT License. See LICENSE.md.

