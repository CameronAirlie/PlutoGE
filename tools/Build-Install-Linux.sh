#!/usr/bin/env bash
# Build and install the editor for the current user, including its menu entry.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: tools/Build-Install-Linux.sh [--desktop] [--jobs N]

Builds linux-release and installs to ~/.local/opt/PlutoGE.
Creates an application-menu launcher in $XDG_DATA_HOME/applications
(default: ~/.local/share/applications).

  --desktop   Also copy the launcher to your XDG desktop directory.
  --jobs N    Override parallel compiler jobs (default: CPU/RAM based).
  --help      Show this help.

Run as your normal user, not with sudo. Install the dependencies documented
in README.md first, including .NET 8 and slangc on PATH.
EOF
}

desktop=false
jobs="$(getconf _NPROCESSORS_ONLN)"
# Large C++ translation units need substantial memory. Reserve roughly 1 GiB
# for other work and budget 2 GiB per compiler, capped by the CPU count.
# This is a conservative starting point, not a hard memory guarantee.
if [[ -r /proc/meminfo ]]; then
    available_kib=$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo)
    if [[ $available_kib =~ ^[0-9]+$ ]]; then
        memory_jobs=$(( (available_kib - 1048576) / 2097152 ))
        ((memory_jobs > 0)) || memory_jobs=1
        if ((memory_jobs < jobs)); then jobs=$memory_jobs; fi
    fi
fi
while (($#)); do
    case "$1" in
        --desktop) desktop=true; shift ;;
        --jobs)
            if [[ ${2:-} != +([0-9]) || ${2:-0} =~ ^0+$ ]]; then
                echo '--jobs requires a positive integer.' >&2
                exit 1
            fi
            jobs="$2"; shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
    esac
done

[[ $(uname -s) == Linux ]] || { echo 'This script requires Linux.' >&2; exit 1; }
((EUID != 0)) || { echo 'Run this script without sudo.' >&2; exit 1; }
for tool in cmake git make c++ dotnet slangc; do
    command -v "$tool" >/dev/null || { echo "Missing dependency: $tool (see README.md)." >&2; exit 1; }
done
if ! dotnet --list-sdks | grep -q '^8\.'; then
    echo 'Install the .NET 8 SDK (Arch: dotnet-sdk-8.0).' >&2
    exit 1
fi

desktop_dir=''
if "$desktop"; then
    command -v xdg-user-dir >/dev/null || { echo '--desktop requires xdg-user-dirs.' >&2; exit 1; }
    desktop_dir="$(xdg-user-dir DESKTOP)"
    if [[ -z $desktop_dir || $desktop_dir == "$HOME" || $desktop_dir != /* ]]; then
        echo 'No dedicated XDG desktop directory is configured.' >&2
        exit 1
    fi
fi

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
prefix="$HOME/.local/opt/PlutoGE"
data_home="${XDG_DATA_HOME:-$HOME/.local/share}"
[[ $data_home == /* ]] || { echo 'XDG_DATA_HOME must be an absolute path.' >&2; exit 1; }
cd "$repo_root"

# Installed native hosts resolve the bundled OpenAL library beside themselves.
cmake --preset linux-release '-DCMAKE_INSTALL_RPATH=$ORIGIN'
printf 'Building editor and runtime dependencies with %s parallel jobs.\n' "$jobs"
cmake --build --preset linux-release --target PlutoGEEditor --parallel "$jobs"
cmake --install out/build/linux-release --prefix "$prefix"

mkdir -p "$prefix/Shaders"
cp -a out/build/linux-release/engine/render/shaders/. "$prefix/Shaders/"
# CMake installs OpenAL's versioned file, but not its SONAME symlink.
for library in "$prefix"/libopenal.so.*.*; do
    [[ -f $library ]] || continue
    ln -sfn "${library##*/}" "$prefix/libopenal.so.1"
done
install -Dm644 packaging/branding/icon_512.png "$prefix/icon.png"

# Desktop entries have their own escaping rules, separate from shell quoting.
desktop_string() {
    local value="$1"
    value=${value//\\/\\\\}
    value=${value//$'\n'/\\n}
    value=${value//$'\r'/\\r}
    value=${value//$'\t'/\\t}
    printf '%s' "$value"
}
desktop_exec() {
    local value="$1"
    value=${value//\\/\\\\}
    value=${value//\"/\\\"}
    value=${value//\$/\\\$}
    value=${value//\`/\\\`}
    value=${value//%/%%}
    desktop_string "$value"
}
launcher="$data_home/applications/plutoge.desktop"
mkdir -p "${launcher%/*}"
cat > "$launcher" <<EOF
[Desktop Entry]
Type=Application
Name=PlutoGE Editor
Comment=Create games with PlutoGE
Exec="$(desktop_exec "$prefix/PlutoGEEditor")"
Path=$(desktop_string "$prefix")
Icon=$(desktop_string "$prefix/icon.png")
Terminal=false
Categories=Development;Graphics;
EOF
chmod 644 "$launcher"
if "$desktop"; then
    install -Dm755 "$launcher" "$desktop_dir/plutoge.desktop"
    echo 'Your desktop may require you to right-click the shortcut and allow launching.'
fi
printf '\nInstalled PlutoGE to %s\nLaunch PlutoGE Editor from your application menu.\n' "$prefix"
