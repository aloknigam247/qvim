# qvim — Setup & Build Runbook

> All steps run in **PowerShell 7 (`pwsh`)** on Windows. Versions below are the ones that actually built successfully (May 2026).

## 1. Install Visual Studio Build Tools 2026

> ⚠️ Don't use `winget install Microsoft.VisualStudio.BuildTools --override "--quiet ..."` — winget reports success once the bootstrapper downloads, but the actual workload install silently fails to land. Use the direct bootstrapper instead.

Download and run the bootstrapper directly:

```pwsh
$dest = "$env:TEMP\vs_buildtools.exe"
Invoke-WebRequest -Uri "https://aka.ms/vs/18/stable/vs_buildtools.exe" -OutFile $dest -UseBasicParsing
Start-Process -FilePath $dest -ArgumentList @(
  "--passive","--norestart","--wait",
  "--add","Microsoft.VisualStudio.Workload.VCTools","--includeRecommended",
  "--add","Microsoft.VisualStudio.Component.Windows11SDK.26100"
) -Verb RunAs -Wait
```

UAC prompt → Yes. A VS installer UI appears with progress; leave it open until done (~15 min, ~7 GB).

Verify (`vswhere` needs `-prerelease` because VS 2026 is still flagged that way in some catalogs):

```pwsh
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -all -prerelease -products * -format value -property installationPath
```

Should print `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools`. The MSVC toolset lands at `…\18\BuildTools\VC\Tools\MSVC\14.51.36231\`.

## 2. Install CMake 4.3.x (required for the VS 2026 generator)

Your existing CMake 3.31.6 is too old — the `Visual Studio 18 2026` generator was added in CMake 4.2.

```pwsh
winget install --id Kitware.CMake --version 4.3.2 --accept-package-agreements --accept-source-agreements
```

UAC → Yes. Verify:

```pwsh
cmake --version   # should print 4.3.x
```

## 3. Bootstrap vcpkg

```pwsh
git clone https://github.com/microsoft/vcpkg.git D:\vcpkg
& D:\vcpkg\bootstrap-vcpkg.bat
[Environment]::SetEnvironmentVariable("VCPKG_ROOT", "D:\vcpkg", "User")
```

**Open a new PowerShell window** so `VCPKG_ROOT` propagates.

## 4. Pin the vcpkg baseline

```pwsh
$sha = git -C $env:VCPKG_ROOT rev-parse HEAD
(Get-Content D:\qvim\vcpkg-configuration.json -Raw) -replace 'REPLACE_WITH_CURRENT_BASELINE_COMMIT_SHA', $sha | Set-Content D:\qvim\vcpkg-configuration.json -NoNewline
```

> Note: the dependency in `vcpkg.json` is named **`msgpack`** (not `msgpack-cxx`). The vcpkg port "msgpack" v7.0.0 IS the C++ msgpack-cxx library; the CMake `find_package(msgpack-cxx CONFIG REQUIRED)` call in `CMakeLists.txt` still works.

## 5. First configure — slow (Qt compile)

```pwsh
cd D:\qvim
cmake --preset dev
```

The first configure runs vcpkg, which **compiles Qt 6.10.3 from source — expect 1–3 hours**. Subsequent configures reuse the cache (~10s).

Produces `D:\qvim\build\dev\qvim.sln` plus MSBuild project files.

## 6. Build

```pwsh
cmake --build --preset dev
```

Produces `D:\qvim\build\dev\Debug\qvim.exe` and 11 test binaries. With `windeployqt` wired into post-build (see CMakeLists.txt), Qt DLLs + plugins are copied next to each `.exe` automatically — no env vars needed at launch.

## 7. Run the tests

```pwsh
ctest --preset dev
```

- 4 unit tests (Tier 1, <2s)
- 6 integration tests with real `nvim --embed --clean` (Tier 2, ~15s)
- 1 QML test suite covering 3 components (Tier 3, ~5s)

Total wall clock: <30s. Expected result: **11/11 passing**.

## 8. Launch

```pwsh
D:\qvim\build\dev\Debug\qvim.exe
```

A window opens, attaches to nvim, and you'll see a `[No Name]` buffer with cursor in normal mode.

## Troubleshooting

| Symptom | Likely fix |
|---------|-----------|
| `Generator: Visual Studio 18 2026 not found` | CMake too old. Upgrade to 4.2+ (step 2). |
| vcpkg complains the baseline doesn't contain `msgpack-cxx` | The vcpkg port is named `msgpack` (not `msgpack-cxx`). Already corrected in `vcpkg.json`. |
| `find_package(Qt6 6.11 ...)` version mismatch | vcpkg baseline ships Qt 6.10.3, not 6.11.x. `CMakeLists.txt` is set to `6.10`. |
| Test executables hang at exactly 60s timeout | DLL not found at launch. `windeployqt` post-build step should prevent this; verify it ran. |
| MSVC Runtime Library error dialog at launch | Missing platform plugin. Confirm `qwindowsd.dll` exists next to `qvim.exe` under `platforms/`. |
| `qvim.exe` runs but window doesn't appear (Hwnd=0) | QML failed to load. Run with `QT_FORCE_STDERR_LOGGING=1 QT_ASSUME_STDERR_HAS_CONSOLE=1` and read stderr. |
| `Module "Qvim" plugin "qvim_libplugin" not found` | The plugin needs to be statically linked into the consumer. Both `qvim` and `test_qml` link it in `CMakeLists.txt`. |
| Orphan `nvim.exe` processes after qvim crashes | Normal — kill them: `Get-Process nvim \| Stop-Process -Force`. The main app now wires `disconnected → app.quit()`, so this only happens on crashes. |

## Updating dependencies later

```pwsh
git -C $env:VCPKG_ROOT pull
$sha = git -C $env:VCPKG_ROOT rev-parse HEAD
(Get-Content D:\qvim\vcpkg-configuration.json -Raw) -replace '"baseline": "[a-f0-9]+"', "`"baseline`": `"$sha`"" | Set-Content D:\qvim\vcpkg-configuration.json -NoNewline
cmake --preset dev   # vcpkg picks up the new baseline
```
