# imgui_test_engine — vendored (Debug-only, may be absent)

- Upstream: https://github.com/ocornut/imgui_test_engine
- Pin: `v1.92.9`
- Used ONLY by `GOSURVEY_DEVELOPER_SHELL` (Debug / `ninja-debug`) — REQ-161 / ADR-040.
  Release and CI never touch it. If this directory is empty a Release build is fine;
  a Debug build with the Developer Shell fails with a clear message.

## To populate
Check out `v1.92.9` and copy its `imgui_test_engine/` subdirectory to
`third_party/imgui_test_engine/imgui_test_engine/` (engine `.cpp`/`.h`, the
`thirdparty/Str/` headers, and `LICENSE.txt`). No CMakeLists needed.
