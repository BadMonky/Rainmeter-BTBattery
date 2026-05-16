# Packaging BTBatteryExample.rmskin

Use Rainmeter's built-in Skin Packager. The package file must be created by Rainmeter; renaming a ZIP file to `.rmskin` is not valid.

## Inputs

Header image:

```text
assets\BTBatteryExampleHeader.bmp
```

Skin to add:

```text
BTBatteryExample
```

Custom plugins to add:

```text
Release\x64\BTBattery.dll
Release\x86\BTBattery.dll
```

Suggested package metadata:

```text
Name: BTBatteryExample
Author: BadMonky
Version: 1.0.0
Minimum Rainmeter: 4
License: MIT
```

## Steps

1. Open Rainmeter Manage.
2. Click `Create .rmskin package...`.
3. Add the `BTBatteryExample` skin.
4. Add both custom plugin DLLs.
5. On the Advanced tab, select the header image.
6. Create the package.

Rainmeter will install the correct plugin architecture when users install the `.rmskin`.
