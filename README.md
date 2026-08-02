# Creation Texture

`Creation Texture` is a Creation Suite application scaffold generated from the
shared project template.

## What This Starter Includes

- shared Creation Suite header shell
- shared suite settings and EULA access
- shared AI account and routing configuration hooks
- shared project registry visibility
- app-local language host policy boundary
- `config/`, `tests/`, and `deploy/` starter folders

## Building

```powershell
$env:JUCE_DIR="D:\JUCE2\JUCE"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

## Status

Shell scaffold only. This project is intended to give a new suite app the
common platform capabilities immediately so domain work can start from a
consistent base.

