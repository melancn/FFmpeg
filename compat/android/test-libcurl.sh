#!/usr/bin/env bash
# Exercise the dependency verification guards without an NDK or Android runner.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERIFY="$SCRIPT_DIR/verify-libcurl.sh"
TEMP_ROOT="$(cd "${TMPDIR:-/tmp}" && pwd -P)"
FIXTURE="$(mktemp -d "$TEMP_ROOT/ffmpeg-libcurl-test.XXXXXX")"
FIXTURE="$(cd "$FIXTURE" && pwd -P)"
cleanup() {
    # Resolve and constrain the recursive cleanup to the fixture we created.
    if [ "$(dirname "$FIXTURE")" = "$TEMP_ROOT" ] &&
       [[ "$(basename "$FIXTURE")" == ffmpeg-libcurl-test.* ]]; then
        rm -rf -- "$FIXTURE"
    fi
}
trap cleanup EXIT
export CURL_PREFIX="$FIXTURE/curl" MBEDTLS_PREFIX="$FIXTURE/mbedtls"
export CURL_VERSION=8.22.0 NM="$FIXTURE/bin/llvm-nm"
export FAKE_VERSION="$CURL_VERSION" FAKE_PROTOCOLS='HTTP HTTPS'
export FIXTURE
mkdir -p "$FIXTURE/bin" "$CURL_PREFIX/lib/pkgconfig" "$CURL_PREFIX/include/curl" \
    "$CURL_PREFIX/share/android-build" "$CURL_PREFIX/share/licenses/curl"
touch "$CURL_PREFIX/lib/libcurl.a" "$CURL_PREFIX/include/curl/curl.h" \
    "$CURL_PREFIX/share/licenses/curl/COPYING" "$CURL_PREFIX/share/android-build/libcurl.txt"
cat > "$FIXTURE/bin/pkg-config" <<'EOF'
#!/usr/bin/env bash
set -eu
# The verifier must replace host pkg-config paths, including on cache hits.
expected="$CURL_PREFIX/lib/pkgconfig:$MBEDTLS_PREFIX/lib/pkgconfig"
[ "$PKG_CONFIG_PATH" = "$expected" ]
[ "$PKG_CONFIG_LIBDIR" = "$expected" ]
[ -z "${PKG_CONFIG_SYSROOT_DIR:-}" ]
case "$*" in
    '--modversion libcurl') echo "$FAKE_VERSION" ;;
    '--variable=supported_protocols libcurl') echo "$FAKE_PROTOCOLS" ;;
    '--static --libs libcurl') echo '-lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz' ;;
    *) exit 1 ;;
esac
EOF
cat > "$NM" <<'EOF'
#!/usr/bin/env bash
cat "$FIXTURE/symbols"
EOF
chmod +x "$FIXTURE/bin/pkg-config" "$NM"
export PATH="$FIXTURE/bin:$PATH"
CONFIG="$CURL_PREFIX/share/android-build/curl_config.h"
cat > "$FIXTURE/good-config" <<'EOF'
#define USE_MBEDTLS 1
#define USE_IPV6 1
#define USE_RESOLV_THREADED 1
#define HAVE_LIBZ 1
/* #undef CURL_DISABLE_PROXY */
/* #undef CURL_DISABLE_HTTP */
/* #undef CURL_DISABLE_HTTP_AUTH */
/* #undef CURL_DISABLE_BASIC_AUTH */
EOF
cat > "$FIXTURE/good-symbols" <<'EOF'
00000000 T curl_easy_init
00000000 T curl_easy_setopt
00000000 T curl_multi_init
00000000 T Curl_cf_socks_proxy_insert_after
EOF
cp "$FIXTURE/good-config" "$CONFIG"
cp "$FIXTURE/good-symbols" "$FIXTURE/symbols"
export PKG_CONFIG_PATH=/host/lib/pkgconfig PKG_CONFIG_LIBDIR=/host/lib/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=/host/sysroot
bash "$VERIFY" > "$FIXTURE/output" 2>&1
echo 'PASS: valid static dependency and host pkg-config isolation'

expect_failure() {
    local expected="$1"
    if bash "$VERIFY" > "$FIXTURE/output" 2>&1; then
        echo "Expected verification failure: $expected" >&2
        exit 1
    fi
    if ! grep -Fq "$expected" "$FIXTURE/output"; then
        cat "$FIXTURE/output" >&2
        echo "Unexpected failure instead of: $expected" >&2
        exit 1
    fi
    echo "PASS: rejects $expected"
}
FAKE_VERSION=0.0.0 expect_failure 'libcurl version mismatch'
FAKE_PROTOCOLS=HTTP expect_failure 'Missing libcurl protocol: HTTPS'
for flag in USE_MBEDTLS USE_IPV6 USE_RESOLV_THREADED HAVE_LIBZ; do
    grep -v "$flag" "$FIXTURE/good-config" > "$CONFIG"
    expect_failure "Missing libcurl feature: $flag"
done
for flag in CURL_DISABLE_PROXY CURL_DISABLE_HTTP CURL_DISABLE_HTTP_AUTH CURL_DISABLE_BASIC_AUTH; do
    cp "$FIXTURE/good-config" "$CONFIG"
    echo "#define $flag 1" >> "$CONFIG"
    expect_failure "Required libcurl feature disabled: $flag"
done
cp "$FIXTURE/good-config" "$CONFIG"
for symbol in curl_easy_init curl_easy_setopt curl_multi_init Curl_cf_socks_proxy_insert_after; do
    grep -v " $symbol$" "$FIXTURE/good-symbols" > "$FIXTURE/symbols"
    expect_failure "Missing libcurl symbol: $symbol"
done
cp "$FIXTURE/good-symbols" "$FIXTURE/symbols"
touch "$CURL_PREFIX/lib/libcurl.so.4"
expect_failure 'libcurl must be static'
echo 'All 16 libcurl verification cases passed.'
