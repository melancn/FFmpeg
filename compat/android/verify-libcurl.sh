#!/usr/bin/env bash
# Static checks only: Android archives cannot be executed on the Linux runner.
set -euo pipefail
: "${CURL_PREFIX:?CURL_PREFIX is required}"
: "${MBEDTLS_PREFIX:?MBEDTLS_PREFIX is required}"
: "${CURL_VERSION:?CURL_VERSION is required}"
: "${NM:?NM must be the NDK llvm-nm}"

export PKG_CONFIG_PATH="$CURL_PREFIX/lib/pkgconfig:$MBEDTLS_PREFIX/lib/pkgconfig"
export PKG_CONFIG_LIBDIR="$PKG_CONFIG_PATH"
unset PKG_CONFIG_SYSROOT_DIR
test -f "$CURL_PREFIX/lib/libcurl.a"
test -f "$CURL_PREFIX/include/curl/curl.h"
test -f "$CURL_PREFIX/share/licenses/curl/COPYING"
if compgen -G "$CURL_PREFIX/lib/libcurl.so*" > /dev/null; then
    echo 'libcurl must be static, not an additional APK shared library' >&2
    exit 1
fi
actual_version="$(pkg-config --modversion libcurl)"
if [ "$actual_version" != "$CURL_VERSION" ]; then
    echo "libcurl version mismatch: expected $CURL_VERSION, found $actual_version" >&2
    exit 1
fi
config="$CURL_PREFIX/share/android-build/curl_config.h"
for flag in USE_MBEDTLS USE_IPV6 USE_RESOLV_THREADED HAVE_LIBZ; do
    if ! grep -Eq "^#define $flag 1$" "$config"; then
        echo "Missing libcurl feature: $flag" >&2
        exit 1
    fi
done
for flag in CURL_DISABLE_PROXY CURL_DISABLE_HTTP CURL_DISABLE_HTTP_AUTH CURL_DISABLE_BASIC_AUTH; do
    if grep -Eq "^#define $flag( |$)" "$config"; then
        echo "Required libcurl feature disabled: $flag" >&2
        exit 1
    fi
done
protocols="$(pkg-config --variable=supported_protocols libcurl)"
for protocol in HTTP HTTPS; do
    if ! grep -Eiq "(^|[[:space:]])$protocol([[:space:]]|$)" <<< "$protocols"; then
        echo "Missing libcurl protocol: $protocol" >&2
        exit 1
    fi
done
symbols="$("$NM" --defined-only "$CURL_PREFIX/lib/libcurl.a")"
for symbol in curl_easy_init curl_easy_setopt curl_multi_init Curl_cf_socks_proxy_insert_after; do
    if ! grep -Eq "[[:space:]]$symbol$" <<< "$symbols"; then
        echo "Missing libcurl symbol: $symbol" >&2
        exit 1
    fi
done
pkg-config --static --libs libcurl
cat "$CURL_PREFIX/share/android-build/libcurl.txt"
echo 'Verified static libcurl: HTTP/HTTPS, proxy/auth, SOCKS, mbedTLS, IPv6, threaded DNS, zlib'
