# shellcheck shell=bash
# Shared helpers for the dev/ tooling layer.
#
# GoSurvey is a Windows-native application (MSVC + CMake + Ninja). These scripts
# are a thin adapter so an AI coding agent running under WSL / Linux / Git Bash
# can drive the *existing* Windows build, test and GitHub tooling without
# re-implementing any of it. See dev/README.md.

set -euo pipefail

# Repository root (this file lives in <root>/dev/).
DEV_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$DEV_DIR/.." && pwd)"

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
info() { printf '\033[36m[dev]\033[0m %s\n' "$*" >&2; }

# --- environment detection ---------------------------------------------------
# WSL   : real Linux kernel with Windows interop (.exe on PATH, /mnt/c mounts)
# MSYS  : Git Bash / MSYS2 on Windows (native Windows paths via cygpath)
# LINUX : plain Linux with no Windows access (scripts that need Windows fail loud)
detect_env() {
  case "$(uname -s)" in
    *NT*|MSYS*|MINGW*|CYGWIN*) echo msys ;;
    Linux)
      if grep -qiE 'microsoft|wsl' /proc/version 2>/dev/null; then echo wsl
      else echo linux; fi ;;
    *) echo linux ;;
  esac
}
DEV_ENV="$(detect_env)"

# Convert a path in this shell's namespace to a Windows path (C:\...).
to_win_path() {
  local p="$1"
  case "$DEV_ENV" in
    msys)  cygpath -w "$p" ;;
    wsl)   wslpath -w "$p" ;;
    *)     echo "$p" ;;
  esac
}

REPO_ROOT_WIN="$(to_win_path "$REPO_ROOT" 2>/dev/null || echo "$REPO_ROOT")"

# --- locating Windows executables ------------------------------------------
# Resolve a Windows program (e.g. cmd.exe, gh.exe). Checks PATH first, then a
# few well-known install locations reachable from WSL via /mnt/c.
find_win_exe() {
  local name="$1"; shift
  local hit
  hit="$(command -v "$name" 2>/dev/null || true)"
  if [ -n "$hit" ]; then echo "$hit"; return 0; fi
  local base
  for base in "${@:-}"; do
    [ -n "$base" ] && [ -x "$base" ] && { echo "$base"; return 0; }
  done
  return 1
}

WIN_MNT="/mnt/c"; [ "$DEV_ENV" = msys ] && WIN_MNT="/c"

require_cmd_exe() {
  CMD_EXE="$(find_win_exe cmd.exe "$WIN_MNT/Windows/System32/cmd.exe")" \
    || die "cmd.exe was not found. This command needs the Windows shell.
       Run from WSL with Windows interop enabled, or from Git Bash on Windows."
}

require_gh_exe() {
  GH_EXE="$(find_win_exe gh.exe \
      "$WIN_MNT/Program Files/GitHub CLI/gh.exe" \
      "$WIN_MNT/Program Files (x86)/GitHub CLI/gh.exe")" \
    || die "gh.exe (Windows GitHub CLI) was not found.
       Install it on Windows:  winget install --id GitHub.cli
       The dev/ layer intentionally uses the user's existing Windows gh.exe and
       will not install a separate Linux GitHub CLI."
}

# Run cmd.exe /c "<command>" from the Windows repo root, forwarding exit code.
#
# cmd.exe inherits its working directory from this process, so we cd the shell
# to the repo root rather than embedding a `cd` in the command string — MSYS
# mangles embedded quotes, and WSL launches interop children in the caller's
# directory too. If cmd still lands elsewhere (e.g. repo on a native-Linux
# path under WSL), it re-homes with an unquoted `cd` first.
#
# The command is written to a temporary .cmd file rather than passed as a
# `/c "string"`: the MSYS/Git Bash runtime rewrites embedded double quotes to
# \" on the way to a native .exe, which corrupts any Windows command that
# quotes a path. File contents are not touched, so this is quote-safe in every
# environment. The batch file cd's to the repo root itself so cmd's inherited
# working directory does not matter.
win_cmd() {
  require_cmd_exe
  local bat rc
  # Created inside the repo root, not $TMPDIR: under WSL a /tmp path converts to
  # a \\wsl.localhost\... UNC path, and cmd.exe refuses to run a batch file from
  # a UNC location. The repo root is Windows-accessible by definition (the build
  # writes there). .dev-win.* is gitignored.
  bat="$(mktemp "$REPO_ROOT/.dev-win.XXXXXX")" || die "could not create a temp file"
  mv "$bat" "$bat.cmd"; bat="$bat.cmd"
  {
    printf '@echo off\r\n'
    printf 'cd /d "%s"\r\n' "$REPO_ROOT_WIN"
    printf '%s\r\n' "$*"
  } > "$bat"
  local batwin; batwin="$(to_win_path "$bat")"
  set +e
  # batwin unquoted on purpose: a quoted arg would be \"-escaped by MSYS. The
  # temp path lives under $TMPDIR and contains no spaces.
  MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' "$CMD_EXE" /d /c $batwin
  rc=$?
  set -e
  rm -f "$bat"
  return $rc
}
