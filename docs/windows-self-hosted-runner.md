# Use a spare Windows 11 laptop as a build runner

GitHub's runner makes an outbound HTTPS connection to receive jobs. The laptop
does not need a public IP, router port forwarding, or a public RDP server.
Tailscale is optional for remote administration.

This guide prepares a machine; the repository's required jobs continue to use
GitHub-hosted runners until their routing and cache handling are adapted and
validated. Installing the runner alone does not move jobs onto it.

## 1. Install the build environment

Use Windows 11 x64, an SSD, at least 8 GB RAM, and at least 30 GB free space.
16 GB RAM and additional free space are preferable for concurrent compilation.
Use a dedicated build machine/account without personal credentials or files.

Download `tools/setup-windows-build-runner.ps1` from this branch, or run it from
a checkout. Open **PowerShell as administrator**:

```powershell
# Inspect the plan first; this makes no changes.
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\setup-windows-build-runner.ps1 -PlanOnly

# Install tools, Qt, and launchers; prevent automatic sleep while plugged in.
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\setup-windows-build-runner.ps1 -PreventSleep
```

For a downloaded standalone copy, replace `tools\setup-windows-build-runner.ps1`
with its actual location. If WinGet is missing, install/update Microsoft's
[App Installer](https://apps.microsoft.com/detail/9nblggh4nns1) first.

The script installs Git, CMake, Ninja, PowerShell 7, Python 3.13, LLVM/clang-tidy,
sccache, GitHub CLI, and 7-Zip using WinGet. It uses an existing MSVC C++ toolchain
or installs Visual Studio 2022 Build Tools with the C++ workload and Windows SDK.
It installs the same Qt 6.8.3 MSVC x64 kit used in CI through an isolated aQt
environment. Existing package installations are checked before installing.

Defaults: helper files under `C:\GoSurveyCI`, Qt under `C:\Qt`. Override with
`-InstallRoot` and `-QtDirectory` if needed. If an installer requests a restart,
restart Windows and rerun the script. The script never restarts Windows itself.

`-PreventSleep` changes plugged-in sleep and hibernate timeouts. Check Windows'
lid-close setting separately if the laptop will run with its lid closed, and
keep it powered and ventilated. No network listener or firewall port is added.

## 2. Register the laptop with GitHub

Sign into the Windows account that will run jobs and open a **normal,
non-administrator PowerShell 7 window**. Use the runner account's own folder,
such as `$env:USERPROFILE\actions-runner`.

1. Open [GoSurvey's runner registration page](https://github.com/chetjones003/GoSurvey/settings/actions/runners/new).
2. Select **Windows**, **x64**.
3. Follow GitHub's current download, checksum, extract, and `config.cmd`
   instructions in that folder. The generated registration token expires after
   one hour; enter it on the laptop, not in chat or a committed script.
4. Give it a name such as `gosurvey-laptop` and an additional label
   `gosurvey-laptop`.
5. When configuration asks to run as a Windows service, choose **No** for this
   initial GUI-test setup.

Our Qt launcher creates a private desktop but explicitly launches its child on
`winsta0`. Running that launcher from service session 0 has not been validated.
An interactive logged-in runner is the initial supported configuration for this
experiment. Lock/disconnect behavior and graphics must be checked on the actual
laptop before unattended use.

Start the runner:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File 'C:\GoSurveyCI\Start-GoSurveyRunner.ps1'
```

Use `-RunnerDirectory` if its folder differs. GitHub should show the runner as
**Idle**, and the terminal should say **Listening for Jobs**. Keep this session
running; after restarting Windows, sign in and launch it again.

## 3. Validate the hardware before moving required jobs

The helper `Enter-GoSurveyEnvironment.ps1` loads the MSVC environment and Qt,
uses a per-user local sccache outside the checkout, and sets conservative worker
counts based on the machine's CPU/RAM. Dot-source it for manual builds:

```powershell
Set-ExecutionPolicy Bypass -Scope Process
. 'C:\GoSurveyCI\Enter-GoSurveyEnvironment.ps1'
git clone https://github.com/chetjones003/GoSurvey.git
Set-Location GoSurvey
git switch beta
cmake --preset ninja-release -DGOSURVEY_BUILD_TESTS=ON -DGOSURVEY_USE_PCH=OFF -DCMAKE_C_COMPILER_LAUNCHER=sccache -DCMAKE_CXX_COMPILER_LAUNCHER=sccache "-DCMAKE_PREFIX_PATH=$env:GOSURVEY_QT_ROOT"
cmake --build --preset ninja-release --target GoSurveyQt GoSurveyTests gosurvey_headless GoSurveyQtTests
ctest --preset ninja-release
sccache --show-stats
```

Measure a clean build, an incremental rebuild, and full tests. Windows 11 uses
the laptop's graphics driver; OpenGL viewport tests need working graphics. A
headless VPS may need the pinned Mesa deployment already used in the hosted CI
workflow. The setup script does not deploy a replacement system graphics driver.

## 4. Route trusted jobs deliberately

The initial `gosurvey-victus-ci` laptop is validated separately by
`.github/workflows/self-hosted-benchmark.yml`. Its bootstrap push trigger is
limited to `chore/self-hosted-runner-benchmark`, and the job accepts only the
repository owner's actor in this repository. It never runs on a PR event.
The job checks normal interactive execution before checkout, uses the uniquely
labeled laptop, and uploads preflight/build/cache/CTest evidence. A future manual
dispatch becomes available when the workflow reaches the default branch.

`tools/benchmark-windows-runner.ps1` can also be run locally from a normal
PowerShell 7 session. It times an initial build and a `--clean-first` rebuild with
the local cache, then runs the complete CTest suite using native graphics. The
default test concurrency is capped at eight. It preserves the external compiler
cache and writes reports under `build/artifacts/runner-benchmark/`. Treat the first
build as cold only when its actual cache counters show that. Keep separate
checkout/build trees for simultaneous runner instances.

A runner with the additional label above can be selected with:

```yaml
runs-on: [self-hosted, Windows, X64, gosurvey-laptop]
```

Do not apply this broadly to the public repository's PR jobs. PR code can execute
arbitrary commands and compromise a persistent machine. Use an isolated runner
for trusted work and retain hosted or freshly isolated execution for untrusted
PRs; keeping secrets out of a workflow alone is insufficient protection.

Migration also needs to reuse the installed Qt, use local compiler caching,
preserve or deliberately restore incremental build trees despite checkout's
default cleanup, and tune the workflow's explicit worker limits. Each registered
runner accepts one job at a time; multiple instances require separate workspaces
and a shared CPU/RAM budget. All required checks must continue to block merge.

### References

- [GitHub: adding a self-hosted runner](https://docs.github.com/en/actions/how-tos/manage-runners/self-hosted-runners/add-runners)
- [GitHub: runner communication and routing](https://docs.github.com/en/actions/reference/runners/self-hosted-runners)
- [GitHub: securing self-hosted runners](https://docs.github.com/en/actions/reference/security/secure-use#hardening-for-self-hosted-runners)
- [Microsoft: WinGet install options](https://learn.microsoft.com/en-us/windows/package-manager/winget/install)
- [Microsoft: Visual Studio installer parameters](https://learn.microsoft.com/en-us/visualstudio/install/use-command-line-parameters-to-install-visual-studio)
- [aQt: command-line options](https://aqtinstall.readthedocs.io/en/v3.3.0/cli.html)
