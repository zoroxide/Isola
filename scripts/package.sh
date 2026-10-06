#!/usr/bin/env bash
# Bundle the built demo into a self-contained folder + archive:
#   the executable, the shared libraries / DLLs it needs, shaders/ and assets/.
# The demo finds shaders/ and assets/ in the working directory, so the launchers start it from
# the package folder.
#
#   cmake -S . -B build && cmake --build build
#   scripts/package.sh linux   [binary]   (default build/examples/isola-demo)     -> dist/Isola-linux-x86_64.tar.gz
#   scripts/package.sh windows [binary]   (default build/examples/isola-demo.exe) -> dist/Isola-windows-x86_64.zip
# Windows must run inside an MSYS2 UCRT64 shell (DLLs are taken from /ucrt64/bin).
set -euo pipefail

platform="${1:?usage: scripts/package.sh linux|windows [binary]}"
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

name="Isola-${platform}-x86_64"
out="dist/$name"
rm -rf "$out"
mkdir -p "$out"

# Runtime data. Skipped: archives and the unused sample house model.
cp -r shaders "$out/"
cp -r assets "$out/"
rm -rf "$out/assets/objs"
find "$out/assets" -name '*.zip' -delete
cp README.md "$out/"

case "$platform" in
linux)
    bin="${2:-build/examples/isola-demo}"
    cp "$bin" "$out/isola-demo"
    mkdir -p "$out/lib"
    # Bundle the libraries the game links (GLFW, GLEW, Assimp and their dependencies), but not the
    # ones that must come from the user's system: glibc, the C++ runtime (the GPU driver links it
    # too, and needs the system's newer copy) and the OpenGL / X11 / Wayland driver stack.
    system='^(linux-vdso|ld-linux|libc\.|libm\.|libdl\.|libpthread\.|librt\.|libresolv\.|libstdc\+\+|libgcc_s|libGL\.|libGLX|libGLdispatch|libOpenGL|libEGL|libGLU\.|libX|libxcb|libwayland|libdrm|libgbm|libudev|libffi|libbsd|libmd\.|libexpat)'
    ldd "$out/isola-demo" | awk '/=> \//{print $1, $3}' | while read -r soname path; do
        if [[ ! "$soname" =~ $system ]]; then
            cp -L "$path" "$out/lib/$soname"
        fi
    done
    # Look for the bundled libraries next to the binary (and next to each other)
    if command -v patchelf >/dev/null; then
        patchelf --set-rpath '$ORIGIN/lib' "$out/isola-demo"
        for lib in "$out"/lib/*; do patchelf --set-rpath '$ORIGIN' "$lib"; done
    else
        echo "patchelf not found: relying on the launcher's LD_LIBRARY_PATH" >&2
    fi
    cat > "$out/isola-demo.sh" <<'EOF'
#!/bin/sh
# Start the game from its own folder (it loads shaders and assets relative to the working directory)
cd "$(dirname "$(readlink -f "$0")")"
LD_LIBRARY_PATH="$PWD/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" exec ./isola-demo "$@"
EOF
    chmod +x "$out/isola-demo" "$out/isola-demo.sh"
    tar -C dist -czf "dist/$name.tar.gz" "$name"
    echo "dist/$name.tar.gz"
    ;;
windows)
    bin="${2:-build/examples/isola-demo.exe}"
    cp "$bin" "$out/isola-demo.exe"
    # Every DLL from the UCRT64 environment the game depends on (GCC runtime, GLFW, GLEW, Assimp,
    # zlib, ...); Windows' own DLLs live in /c/Windows and are left out
    ldd "$out/isola-demo.exe" | awk '{print $3}' | grep -i '^/ucrt64/' | sort -u | while read -r dll; do
        cp "$dll" "$out/"
    done
    # Double-clicking starts in the exe's folder, so shaders/assets resolve; a .bat for windowed mode
    printf '@echo off\r\ncd /d "%%~dp0"\r\nstart "" isola-demo.exe --windowed\r\n' > "$out/isola-demo (windowed).bat"
    (cd dist && rm -f "$name.zip" && zip -qr "$name.zip" "$name")
    echo "dist/$name.zip"
    ;;
*)
    echo "unknown platform: $platform" >&2
    exit 1
    ;;
esac
