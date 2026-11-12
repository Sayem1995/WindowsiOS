#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Cross-compile libffi + glib + libslirp for arm64-apple-ios into ios/deps/,
# and copy StikJIT.xcframework + build libhusk_rppairing.a for Built-in StikJIT.
set -euo pipefail

export PATH="/opt/homebrew/bin:/usr/local/bin:$HOME/.cargo/bin:$PATH"

# Homebrew lives in /opt/homebrew on Apple Silicon and /usr/local on Intel runners.
PYTHON="$(command -v python3 || true)"
[ -n "$PYTHON" ] || { echo "python3 not found on PATH" >&2; exit 1; }

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK_DEPS="$ROOT/work/ios_deps_build"
DL="$WORK_DEPS/sources"
SRC="$WORK_DEPS/src"
PREFIX="$WORK_DEPS/sysroot"
STAMPS="$WORK_DEPS/stamps"
OUT_DEPS="$ROOT/ios/deps"
HUSK_REF="${HUSK_REF:-/Users/ammaar/.gemini/antigravity/brain/f6b287cc-b4bb-4bf9-b1ba-7daee58236c9/scratch/refs/Husk}"

mkdir -p "$DL" "$SRC" "$PREFIX" "$STAMPS" "$OUT_DEPS/lib" "$OUT_DEPS/include"

ARCH=arm64
SDK=iphoneos
SDKMINVER=17.0
NCPU="$(sysctl -n hw.ncpu)"
SDKROOT="$(xcrun --sdk $SDK --show-sdk-path)"
CFLAGS_TARGET="-target $ARCH-apple-ios$SDKMINVER"

export CC="$(xcrun --sdk $SDK --find clang) $CFLAGS_TARGET"
export CXX="$(xcrun --sdk $SDK --find clang++) $CFLAGS_TARGET"
export OBJCC="$(xcrun --sdk $SDK --find clang)"
export AR="$(xcrun --sdk $SDK --find ar)"
export NM="$(xcrun --sdk $SDK --find nm)"
export RANLIB="$(xcrun --sdk $SDK --find ranlib)"
export STRIP="$(xcrun --sdk $SDK --find strip)"

export CFLAGS="-arch $ARCH -isysroot $SDKROOT -I$PREFIX/include $CFLAGS_TARGET -O2"
export CXXFLAGS="$CFLAGS"
export CPPFLAGS="-arch $ARCH -isysroot $SDKROOT -I$PREFIX/include $CFLAGS_TARGET"
export OBJCFLAGS="$CFLAGS"
export LDFLAGS="-arch $ARCH -isysroot $SDKROOT -L$PREFIX/lib $CFLAGS_TARGET"

export PKG_CONFIG="$(command -v pkg-config)"
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig"
unset PKG_CONFIG_PATH

FFI_URL="https://github.com/libffi/libffi/releases/download/v3.5.0/libffi-3.5.0.tar.gz"
GLIB_URL="https://download.gnome.org/sources/glib/2.83/glib-2.83.0.tar.xz"
SLIRP_URL="https://github.com/utmapp/libslirp/releases/download/v4.9.1-release-mirror/libslirp-v4.9.1.tar.gz"

fetch_unpack() {
    local url="$1"
    local fname
    fname="$(basename "$url")"
    if [ ! -s "$DL/$fname" ]; then
        echo "[fetch] $fname"
        curl -fL --retry 3 -o "$DL/$fname.part" "$url"
        mv "$DL/$fname.part" "$DL/$fname"
    fi
    if [ ! -f "$STAMPS/unpack-$fname" ]; then
        echo "[unpack] $fname"
        tar -xf "$DL/$fname" -C "$SRC"
        touch "$STAMPS/unpack-$fname"
    fi
}

fetch_unpack "$FFI_URL"
fetch_unpack "$GLIB_URL"
fetch_unpack "$SLIRP_URL"

meson_quote() { echo "'$(echo $* | sed "s/ /','/g")'"; }

gen_cross() {
    local cross="$1" system="$2"
    cat > "$cross" <<EOF
[properties]
needs_exe_wrapper = true
[built-in options]
c_args = [$(meson_quote $CFLAGS)]
cpp_args = [$(meson_quote $CXXFLAGS)]
objc_args = [$(meson_quote $OBJCFLAGS)]
objcpp_args = [$(meson_quote $OBJCFLAGS)]
c_link_args = [$(meson_quote $LDFLAGS)]
cpp_link_args = [$(meson_quote $LDFLAGS)]
objc_link_args = [$(meson_quote $LDFLAGS)]
objcpp_link_args = [$(meson_quote $LDFLAGS)]
[binaries]
c = [$(meson_quote $CC)]
cpp = [$(meson_quote $CXX)]
objc = [$(meson_quote $OBJCC)]
objcpp = [$(meson_quote $OBJCC)]
ar = ['$AR']
nm = ['$NM']
ranlib = ['$RANLIB']
strip = ['$STRIP', '-x']
pkg-config = ['$PKG_CONFIG']
pkgconfig = ['$PKG_CONFIG']
python = ['$PYTHON']
[host_machine]
system = '$system'
EOF
    if [ "$system" = "darwin" ]; then
        echo "kernel = 'xnu'" >> "$cross"
        echo "subsystem = 'ios'" >> "$cross"
    fi
    cat >> "$cross" <<EOF
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
EOF
}

CROSS_IOS="$WORK_DEPS/cross-ios.meson"
CROSS_DARWIN="$WORK_DEPS/cross-darwin.meson"
gen_cross "$CROSS_IOS" ios
gen_cross "$CROSS_DARWIN" darwin

# 1. libffi
if [ ! -f "$STAMPS/libffi" ]; then
    echo "[build] libffi-3.5.0"
    ( cd "$SRC/libffi-3.5.0" \
      && ./configure --host=aarch64-apple-darwin --prefix="$PREFIX" --enable-static --disable-shared \
      && make -j"$NCPU" \
      && make install ) > "$WORK_DEPS/libffi.log" 2>&1
    touch "$STAMPS/libffi"
fi

# 2. glib (automatically fetches/builds subprojects proxy-libintl & pcre2 if needed)
if [ ! -f "$STAMPS/glib" ]; then
    echo "[build] glib-2.83.0"
    rm -rf "$SRC/glib-2.83.0/_ios_build"
    ( cd "$SRC/glib-2.83.0" \
      && meson setup _ios_build --cross-file "$CROSS_IOS" --prefix="$PREFIX" \
            --buildtype=release --default-library=static \
            -Dtests=false -Dnls=disabled -Dintrospection=disabled \
            -Dselinux=disabled -Dlibmount=disabled -Ddtrace=disabled \
            -Dman-pages=disabled -Dglib_debug=disabled -Dxattr=false \
      && meson compile -C _ios_build -j "$NCPU" \
      && meson install -C _ios_build ) > "$WORK_DEPS/glib.log" 2>&1
    touch "$STAMPS/glib"
fi

# 3. libslirp
if [ ! -f "$STAMPS/libslirp" ]; then
    echo "[build] libslirp-v4.9.1"
    if [ -f "$HUSK_REF/patches/libslirp-v4.9.1.patch" ] && [ ! -f "$SRC/libslirp-v4.9.1/.patched" ]; then
        ( cd "$SRC/libslirp-v4.9.1" && patch -p1 -N -r - < "$HUSK_REF/patches/libslirp-v4.9.1.patch" || true )
        touch "$SRC/libslirp-v4.9.1/.patched"
    fi
    rm -rf "$SRC/libslirp-v4.9.1/_ios_build"
    ( cd "$SRC/libslirp-v4.9.1" \
      && meson setup _ios_build --cross-file "$CROSS_DARWIN" --prefix="$PREFIX" \
            --buildtype=release --default-library=static \
      && meson compile -C _ios_build -j "$NCPU" \
      && meson install -C _ios_build ) > "$WORK_DEPS/libslirp.log" 2>&1
    touch "$STAMPS/libslirp"
fi

# Stage headers & static archives into ios/deps/
rm -rf "$OUT_DEPS/include/slirp"
cp -R "$PREFIX/include/slirp" "$OUT_DEPS/include/slirp"
for lib in libslirp.a libglib-2.0.a libintl.a libpcre2-8.a libffi.a; do
    if [ -f "$PREFIX/lib/$lib" ]; then
        cp "$PREFIX/lib/$lib" "$OUT_DEPS/lib/$lib"
    fi
done

# Copy StikJIT.xcframework if available
if [ -d "$HUSK_REF/src/app/Frameworks/StikJIT.xcframework" ]; then
    rm -rf "$OUT_DEPS/StikJIT.xcframework"
    ditto "$HUSK_REF/src/app/Frameworks/StikJIT.xcframework" "$OUT_DEPS/StikJIT.xcframework"
fi

echo "=== iOS dependencies staged in $OUT_DEPS ==="
ls -lh "$OUT_DEPS/lib"
