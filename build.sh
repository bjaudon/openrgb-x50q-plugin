#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
OPENRGB_SOURCE="${OPENRGB_SOURCE:-}"
OPENRGB_TAG="${OPENRGB_TAG:-}"
QMAKE="${QMAKE:-}"

if [[ -n "$OPENRGB_TAG" && ! "$OPENRGB_TAG" =~ ^(release_[[:alnum:]._-]+|release_candidate_[[:alnum:]._-]+)$ ]]; then
    printf 'OPENRGB_TAG must be a release tag, such as release_candidate_1.0rc2.\n' >&2
    exit 2
fi

if [[ -z "$OPENRGB_SOURCE" ]]; then
    if [[ -z "$OPENRGB_TAG" ]]; then
        printf 'Set OPENRGB_TAG to a GitLab release tag or OPENRGB_SOURCE to a source tree.\n' >&2
        exit 2
    fi

    source_cache="$SCRIPT_DIR/build/openrgb-source"
    source_dir="$source_cache/OpenRGB-$OPENRGB_TAG"
    archive="$source_cache/OpenRGB-$OPENRGB_TAG.tar.gz"
    mkdir -p "$source_cache"

    if [[ ! -f "$archive" ]]; then
        if ! command -v curl >/dev/null 2>&1; then
            printf 'curl is required to download the OpenRGB release source.\n' >&2
            exit 2
        fi
        curl --fail --location --retry 3 \
            --output "$archive.part" \
            "https://gitlab.com/CalcProgrammer1/OpenRGB/-/archive/$OPENRGB_TAG/OpenRGB-$OPENRGB_TAG.tar.gz"
        mv "$archive.part" "$archive"
    fi

    if [[ ! -f "$source_dir/RGBController/RGBController.cpp" ]]; then
        if ! command -v tar >/dev/null 2>&1; then
            printf 'tar is required to extract the OpenRGB release source.\n' >&2
            exit 2
        fi
        mkdir -p "$source_dir"
        if ! tar -tzf "$archive" >/dev/null; then
            printf 'Invalid OpenRGB source archive: %s\n' "$archive" >&2
            exit 2
        fi
        tar -xzf "$archive" --strip-components=1 -C "$source_dir"
    fi
    OPENRGB_SOURCE="$source_dir"
fi

if [[ ! -f "$OPENRGB_SOURCE/RGBController/RGBController.cpp" ]]; then
    printf 'Set OPENRGB_SOURCE to the matching OpenRGB source tree.\n' >&2
    exit 2
fi

if [[ -z "${OPENRGB_VERSION:-}" ]]; then
    if [[ "$OPENRGB_TAG" == release_candidate_* ]]; then
        OPENRGB_VERSION="${OPENRGB_TAG#release_candidate_}"
    elif [[ "$OPENRGB_TAG" == release_* ]]; then
        OPENRGB_VERSION="${OPENRGB_TAG#release_}"
    elif command -v rpm >/dev/null 2>&1 && rpm -q openrgb >/dev/null 2>&1; then
        OPENRGB_VERSION="$(rpm -q --qf '%{VERSION}' openrgb)"
    else
        printf 'Set OPENRGB_VERSION or use a supported OPENRGB_TAG.\n' >&2
        exit 2
    fi
fi

# Package versions such as 1.0~rc2 omit the tilde in plugin filenames.
OPENRGB_VERSION="${OPENRGB_VERSION//\~/}"
OPENRGB_VERSION="${OPENRGB_VERSION//[^[:alnum:]._-]/}"
if [[ -z "$OPENRGB_VERSION" ]]; then
    printf 'OPENRGB_VERSION did not contain any filename-safe characters.\n' >&2
    exit 2
fi

case "$(uname -s)" in
    Linux) os_name=Linux ;;
    *) printf 'Unsupported build operating system: %s\n' "$(uname -s)" >&2; exit 2 ;;
esac

case "$(uname -m)" in
    x86_64|amd64) arch_name=amd64 ;;
    aarch64|arm64) arch_name=arm64 ;;
    i?86) arch_name=386 ;;
    *) arch_name="$(uname -m)" ;;
esac

if [[ -z "$QMAKE" ]]; then
    for candidate in qmake-qt5 qmake; do
        if command -v "$candidate" >/dev/null 2>&1; then
            QMAKE="$candidate"
            break
        fi
    done
fi
if [[ -z "$QMAKE" ]] || ! command -v "$QMAKE" >/dev/null 2>&1; then
    printf 'Qt 5 qmake not found (override with QMAKE=/path/to/qmake).\n' >&2
    exit 2
fi
qt_version="$("$QMAKE" -query QT_VERSION)"
if [[ "$qt_version" != 5.* ]]; then
    printf 'Qt 5 is required; %s reports Qt %s.\n' "$QMAKE" "$qt_version" >&2
    exit 2
fi
if ! command -v make >/dev/null 2>&1; then
    printf 'make not found.\n' >&2
    exit 2
fi

if [[ -z "${X50Q_PLUGIN_COMMIT:-}" ]]; then
    if git -C "$SCRIPT_DIR" rev-parse --verify HEAD >/dev/null 2>&1; then
        X50Q_PLUGIN_COMMIT="$(git -C "$SCRIPT_DIR" rev-parse --short=7 HEAD)"
    else
        X50Q_PLUGIN_COMMIT=unknown
    fi
fi

if [[ "$X50Q_PLUGIN_COMMIT" =~ ^[[:xdigit:]]{7,40}$ ]]; then
    plugin_commit="${X50Q_PLUGIN_COMMIT:0:7}"
elif [[ "$X50Q_PLUGIN_COMMIT" == unknown ]]; then
    plugin_commit=unknown
else
    printf 'X50Q_PLUGIN_COMMIT must be a hexadecimal commit or "unknown".\n' >&2
    exit 2
fi

if [[ -z "${X50Q_PLUGIN_URL:-}" ]]; then
    X50Q_PLUGIN_URL=
    if remote_url="$(git -C "$SCRIPT_DIR" remote get-url origin 2>/dev/null)"; then
        case "$remote_url" in
            git@*:* )
                remote_host="${remote_url#git@}"
                remote_host="${remote_host%%:*}"
                remote_path="${remote_url#*:}"
                X50Q_PLUGIN_URL="https://${remote_host}/${remote_path}"
                ;;
            ssh://git@*/*)
                remote_path="${remote_url#ssh://git@}"
                remote_host="${remote_path%%/*}"
                remote_path="${remote_path#*/}"
                X50Q_PLUGIN_URL="https://${remote_host}/${remote_path}"
                ;;
            https://*|http://*) X50Q_PLUGIN_URL="$remote_url" ;;
        esac
        X50Q_PLUGIN_URL="${X50Q_PLUGIN_URL%.git}"
    fi
fi

export OPENRGB_SOURCE X50Q_PLUGIN_COMMIT X50Q_PLUGIN_URL
cd "$SCRIPT_DIR"
"$QMAKE" X50QPlugin.pro
make clean
make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')"

output="$SCRIPT_DIR/build/OpenRGBX50QPlugin_${OPENRGB_VERSION}_${os_name}_${arch_name}_${plugin_commit}.so"
rm -f "$SCRIPT_DIR"/build/OpenRGBX50QPlugin_*.so
mv -f "$SCRIPT_DIR/build/libOpenRGBX50QPlugin.so" "$output"
printf 'Built plugin: %s\n' "$output"
