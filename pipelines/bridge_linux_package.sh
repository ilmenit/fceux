#!/usr/bin/env bash
set -euo pipefail
stage=$1
binary=$2
mkdir -p "$stage/runtime/usr/bin" "$stage/bin" build/deploy-tools
cp "$binary" "$stage/runtime/usr/bin/fceux"
strip "$stage/runtime/usr/bin/fceux"
# Use linuxdeploy's dependency resolver and Qt plugin deployment, without FUSE.
for tool in linuxdeploy/linuxdeploy linuxdeploy/linuxdeploy-plugin-qt; do
    name=${tool##*/}-x86_64.AppImage
    curl --fail --location --retry 3 "https://github.com/$tool/releases/download/continuous/$name" -o "build/deploy-tools/$name"
    chmod +x "build/deploy-tools/$name"
done
export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE=/usr/bin/qmake
export EXTRA_PLATFORM_PLUGINS='libqoffscreen.so;libqminimal.so;libqwayland-egl.so;libqwayland-generic.so'
build/deploy-tools/linuxdeploy-x86_64.AppImage \
    --appdir "$stage/runtime" --executable "$stage/runtime/usr/bin/fceux" \
    --plugin qt
# Native Wayland also loads shell/graphics integration plugins at runtime.
qt_plugins=$(qmake -query QT_INSTALL_PLUGINS)
for directory in "$qt_plugins"/wayland-*; do
    [ ! -d "$directory" ] || cp -a "$directory" "$stage/runtime/usr/plugins/"
done
build/deploy-tools/linuxdeploy-x86_64.AppImage --appdir "$stage/runtime" \
    --deploy-deps-only "$stage/runtime/usr/plugins"
# linuxdeploy intentionally excludes libraries common on desktops. The
# Bridge must also run on clean distributions, so deploy all other ELF deps.
extra=$(python3 pipelines/bridge_linux_dependencies.py "$stage/runtime" --prune-os-libraries)
if [ -n "$extra" ]; then
    libraries=()
    while IFS= read -r library; do
        libraries+=(--library "$library")
    done <<< "$extra"
    build/deploy-tools/linuxdeploy-x86_64.AppImage --appdir "$stage/runtime" "${libraries[@]}"
fi
# All remaining external dependencies must be from the explicit OS allowlist.
remaining=$(python3 pipelines/bridge_linux_dependencies.py "$stage/runtime")
test -z "$remaining"
cat > "$stage/bin/fceux" <<'LAUNCHER'
#!/bin/sh
package_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
runtime="$package_dir/runtime/usr"
export LD_LIBRARY_PATH="$runtime/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$runtime/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$runtime/plugins/platforms"
export QML2_IMPORT_PATH="$runtime/qml"
exec "$runtime/bin/fceux" "$@"
LAUNCHER
chmod +x "$stage/bin/fceux"
# The emulator resolves these assets relative to the real binary directory.
for directory in palettes luaScripts tools; do
    ln -sfn "../../$directory" "$stage/runtime/usr/$directory"
done
readelf -d "$stage/runtime/usr/bin/fceux" > "$stage/LINKAGE.txt"
# Check that the reported Lua failure cannot recur, nor missing static libs.
if readelf -d "$stage/runtime/usr/bin/fceux" | grep NEEDED | grep -E 'lib(lua|SDL2|minizip|archive|z)[-.]'; then
    echo 'A portable dependency was unexpectedly linked dynamically' >&2
    exit 1
fi
ldd "$stage/runtime/usr/bin/fceux" | tee "$stage/RUNTIME-LIBRARIES.txt"
! grep -q 'not found' "$stage/RUNTIME-LIBRARIES.txt"

# Preserve redistribution notices for the libraries built into or bundled with
# the Linux package (dpkg licenses include upstream source information).
mkdir -p "$stage/licenses"
for package in libsdl2-dev libminizip-dev libarchive-dev zlib1g-dev libssl-dev libxml2-dev liblzma-dev libzstd-dev liblz4-dev libbz2-dev libacl1-dev nettle-dev qtbase5-dev qtdeclarative5-dev qttools5-dev qtwayland5; do
    if [ -f "/usr/share/doc/$package/copyright" ]; then
        cp "/usr/share/doc/$package/copyright" "$stage/licenses/$package.txt"
    fi
done

cp src/lua/COPYRIGHT "$stage/licenses/Lua.txt"
