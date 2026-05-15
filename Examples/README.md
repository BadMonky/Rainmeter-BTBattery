# BTBattery Rainmeter Example

This example shows how to use the BTBattery plugin to display battery levels for Bluetooth devices.

## Setup

1. Install `BTBattery.dll` in Rainmeter's plugin folder.
2. Copy one of the example measures in `BTBatteryExample.ini`.
3. Set `DeviceName` to the name shown in Windows Bluetooth & devices settings.
4. Refresh the skin.

## Measure Options

```ini
[mDevice]
Measure=Plugin
Plugin=BTBattery
DeviceName=Your Bluetooth Device Name
MatchMode=Exact
PollSeconds=30
EnableLogging=0
```

`DeviceName` is the normal setup option. Use the exact friendly name shown by Windows.

`MatchMode=Exact` is recommended. It avoids accidentally matching the wrong device.

`MatchMode=Contains` or `MatchMode=Partial` can be used if Windows exposes a longer or awkward friendly name and exact matching is inconvenient.

`DeviceAddress` is optional. Add it only when two devices have the same or confusing names.

```ini
DeviceAddress=001122AABBCC
```

`PollSeconds` controls how often the plugin refreshes the shared Bluetooth snapshot in the background. The default example uses 30 seconds.

`EnableLogging=1` writes debug messages to Rainmeter's log. Keep it off for normal use.

## Notes

The first value can briefly show `-1` after Rainmeter starts because the plugin refreshes Bluetooth data on a background thread to avoid UI stutters.

Most devices work through Windows device properties. Devices that hide battery data in custom HID reports require support to be added in a future plugin update.
Some devices may need plugin support. Please open a GitHub issue with logs.
