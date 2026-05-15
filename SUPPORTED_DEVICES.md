# Supported Devices

BTBattery should work with Bluetooth devices that expose the standard Windows battery property.

Known HID report profiles built into this release:

| Device family | USB VID | USB PID | Notes |
| --- | --- | --- | --- |
| Sony DualSense Wireless Controller | `054C` | `0CE6`, `0DF2` | Battery level is read from HID input reports when Windows does not expose the standard Bluetooth battery property. |

If a device shows a battery level in Windows but BTBattery returns `-1`, open a bug report.

If a device needs custom HID parsing, open a device support request and include logs plus any HID report information you can capture.
