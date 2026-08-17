# Building GoSurvey with Telemetry Support

## Prerequisites

- **Windows 11** (or Windows 10 21H2+)
- **Visual Studio 2022** (or Build Tools for Visual Studio 2022) with C++ workload
- **CMake** 3.25+ (`cmake --version`)
- **Ninja** (`ninja --version` or `choco install ninja`)

## Build Steps

### 1. Open Developer Command Prompt
- **Visual Studio 2022**: Start → "Developer Command Prompt for VS 2022"
- **Or manually**: `"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"`

The prompt MUST show `[vcvarsall.bat] Environment initialized...` at startup.

### 2. Configure and Build
```bash
cd C:\Users\chetj\source\repos\GoSurvey

# Configure (first time only, or if CMakeLists.txt changed)
cmake --preset ninja-release

# Build (parallel, 8 threads)
cmake --build build/release --config Release --parallel 8

# Test (optional, but run this to verify telemetry logic)
ctest --test-dir build/release --output-on-failure
```

### 3. Find the Executable
```
build/release/GoSurvey.exe
```

## Troubleshooting

**"cl is not a full path and was not found in the PATH"**
→ You need the Developer Command Prompt (step 1). PowerShell alone won't work.

**"cmake: command not found"**
→ Add CMake to PATH or use the full path: `"C:\Program Files\CMake\bin\cmake.exe"`

**Build succeeds but tests fail**
→ Check `TelemetryPingTests.cpp` output for specific failures. The pure telemetry logic tests should all pass regardless of backend.

## Verifying Telemetry is Built In

After a successful build, run:
```bash
strings build/release/GoSurvey.exe | grep "installId"
```

If you see the JSON schema keywords, telemetry is linked.

## Next: Deploy Your Backend

See `docs/telemetry-backend-guide.md` for free backend options, then update `src/telemetry/TelemetryPing.hpp` with your endpoint URL before the next build.
