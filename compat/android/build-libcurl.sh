#!/usr/bin/env bash
# Build only the Android static libcurl dependency; FFmpeg is built separately.
set -euo pipefail

: "${NDK_ROOT:?NDK_ROOT must point to the Android NDK}"
: "${ANDROID_ABI:?ANDROID_ABI is required}"
: "${CURL_VERSION:?CURL_VERSION must be pinned by the caller}"
: "${CURL_SHA256:?CURL_SHA256 must be pinned by the caller}"
ANDROID_API="${ANDROID_API:-24}"
BUILD_DIR="${BUILD_DIR:-build-android}"
case "$ANDROID_ABI" in
    arm64-v8a|armeabi-v7a) ;;
    *) echo "Unsupported Android ABI: $ANDROID_ABI" >&2; exit 1 ;;
esac
[[ "$CURL_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]
[[ "$CURL_SHA256" =~ ^[[:xdigit:]]{64}$ ]]
[[ "$ANDROID_API" =~ ^[0-9]+$ ]]
test -f "$NDK_ROOT/build/cmake/android.toolchain.cmake"
mkdir -p "$BUILD_DIR"
BUILD_ROOT="$(cd "$BUILD_DIR" && pwd)"
MBEDTLS_PREFIX="${MBEDTLS_PREFIX:-$BUILD_ROOT/mbedtls-install}"
CURL_PREFIX="$BUILD_ROOT/curl-install"
SOURCE="$BUILD_ROOT/curl-$CURL_VERSION"
BINARY="$BUILD_ROOT/curl-build"
ARCHIVE="$BUILD_ROOT/curl-$CURL_VERSION.tar.xz"
for lib in mbedtls mbedx509 mbedcrypto; do
    test -f "$MBEDTLS_PREFIX/lib/lib$lib.a"
    test -f "$MBEDTLS_PREFIX/lib/pkgconfig/$lib.pc"
done

# Never discover host libraries when compiling Android objects.
export PKG_CONFIG_PATH="$MBEDTLS_PREFIX/lib/pkgconfig"
export PKG_CONFIG_LIBDIR="$PKG_CONFIG_PATH"
unset PKG_CONFIG_SYSROOT_DIR
curl --fail --location --retry 3 --connect-timeout 20 --max-time 300 \
    "https://curl.se/download/curl-$CURL_VERSION.tar.xz" -o "$ARCHIVE"
printf '%s  %s\n' "$CURL_SHA256" "$ARCHIVE" | sha256sum --check --strict
mkdir -p "$SOURCE"
tar -xJf "$ARCHIVE" -C "$SOURCE" --strip-components=1

# HTTP_ONLY removes unrelated URL protocols, NOT HTTP CONNECT or SOCKS proxies.
# Reuse the same static mbedTLS archives as FFmpeg; no second TLS implementation.
# CA paths must not be auto-detected from the Linux build host. The application
# must supply its Android trust material via ca_file; verification stays enabled.
cmake -S "$SOURCE" -B "$BINARY" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$NDK_ROOT/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ANDROID_ABI" \
    -DANDROID_PLATFORM="android-$ANDROID_API" \
    -DANDROID_STL=c++_static \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_INSTALL_PREFIX="$CURL_PREFIX" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON \
    -DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF \
    -DBUILD_LIBCURL_DOCS=OFF -DBUILD_MISC_DOCS=OFF -DENABLE_CURL_MANUAL=OFF \
    -DCURL_USE_CMAKECONFIG=OFF -DCURL_USE_PKGCONFIG=ON \
    -DCURL_ENABLE_SSL=ON -DCURL_USE_MBEDTLS=ON -DMBEDTLS_USE_STATIC_LIBS=ON \
    -DCURL_USE_OPENSSL=OFF -DCURL_USE_GNUTLS=OFF -DCURL_USE_WOLFSSL=OFF \
    -DHTTP_ONLY=ON -DCURL_DISABLE_HTTP=OFF \
    -DCURL_DISABLE_PROXY=OFF -DCURL_DISABLE_HTTP_AUTH=OFF \
    -DCURL_DISABLE_BASIC_AUTH=OFF \
    -DENABLE_IPV6=ON -DENABLE_THREADED_RESOLVER=ON -DENABLE_ARES=OFF \
    -DCURL_ZLIB=ON -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF \
    -DCURL_USE_LIBPSL=OFF -DUSE_LIBIDN2=OFF \
    -DCURL_USE_LIBSSH2=OFF -DCURL_USE_LIBSSH=OFF \
    -DUSE_NGHTTP2=OFF -DUSE_NGTCP2=OFF \
    -DCURL_CA_BUNDLE=none -DCURL_CA_PATH=none
cmake --build "$BINARY" --parallel "$(nproc)"
cmake --install "$BINARY"

# Keep feature evidence in the cached install, so cache hits are checked too.
mkdir -p "$CURL_PREFIX/share/android-build" "$CURL_PREFIX/share/licenses/curl"
cp "$BINARY/lib/curl_config.h" "$CURL_PREFIX/share/android-build/"
cp "$SOURCE/COPYING" "$CURL_PREFIX/share/licenses/curl/"
printf 'curl=%s\nsha256=%s\nabi=%s\napi=%s\ntls=mbedtls\n' \
    "$CURL_VERSION" "$CURL_SHA256" "$ANDROID_ABI" "$ANDROID_API" \
    > "$CURL_PREFIX/share/android-build/libcurl.txt"
