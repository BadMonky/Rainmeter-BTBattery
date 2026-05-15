# Source

This folder contains the minimum Rainmeter C++ plugin SDK layout needed to build BTBattery.

```text
API/
  RainmeterAPI.h
  DllExporter.exe
  x64/Rainmeter.lib
  x32/Rainmeter.lib
C++/
  PluginBTBattery/
    PluginBTBattery.cpp
    PluginBTBattery.rc
    PluginBTBattery.vcxproj
    PluginBTBattery.vcxproj.filters
```

Build from this folder with Visual Studio Developer PowerShell or Developer Command Prompt:

```powershell
cd .\C++
msbuild .\PluginBTBattery\PluginBTBattery.vcxproj /p:Configuration=Release /p:Platform=x64
msbuild .\PluginBTBattery\PluginBTBattery.vcxproj /p:Configuration=Release /p:Platform=Win32
```

Outputs are written to:

```text
C++\PluginBTBattery\x64\Release\BTBattery.dll
C++\PluginBTBattery\x32\Release\BTBattery.dll
```
