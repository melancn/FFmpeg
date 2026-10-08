# Android JNI: libcurl dependency

The Android JNI workflow builds libcurl **8.22.0** for `arm64-v8a` and
`armeabi-v7a` (API 24), then embeds it in `libffmpeg_jni.so`. It reuses the
workflow's static mbedTLS 3.6.7 installation. No `libcurl.so`, extra TLS `.so`,
or `libc++_shared.so` needs to be packaged.

## Build

Run `.github/workflows/android.yml` through `workflow_dispatch`, a pull request,
or a push to `master`. The workflow:

1. Downloads the pinned curl archive from curl.se and verifies its pinned SHA-256.
2. Runs `compat/android/build-libcurl.sh` using the NDK CMake toolchain and PIC.
3. Verifies both fresh and cached installs with `verify-libcurl.sh`.
4. Configures FFmpeg with `--enable-libcurl --enable-protocol=libcurl` and
   target-only `pkg-config --static` dependencies.
5. Links libcurl and mbedTLS into the JNI shared library. Before stripping,
   checks the libcurl protocol/API and SOCKS implementation symbols and rejects
   unexpected shared curl/TLS dependencies. Undefined symbols fail linking.
6. Includes curl's `COPYING` and per-ABI build information in both individual
   and combined artifacts.

The curl cache key includes ABI, API, NDK version, curl version/checksum,
mbedTLS version, scripts and workflow. There is no broad cache fallback.
When upgrading curl, update `CURL_VERSION` and `CURL_SHA256` together, review
its CMake options and the internal SOCKS symbol used by the build checks.

Only HTTP/HTTPS URL protocols are built into libcurl. HTTP CONNECT, SOCKS4/5,
SOCKS5 username/password authentication, HTTP Basic authentication, IPv6,
threaded DNS and zlib remain enabled. Optional libpsl, libidn2, SSH, HTTP/2/3,
brotli and zstd dependencies are not introduced. FFmpeg's other existing native
protocols and the libass/MediaCodec builds are unchanged.

## JNI use after installing rebuilt artifacts

The existing dictionary JNI interface is sufficient; no proxy setter is added:

```java
long options = ffmpeg.dictCreate();
try {
    // Check dictSet return values in production code.
    ffmpeg.dictSet(options, "prefer_libcurl", "1", 0);
    ffmpeg.dictSet(options, "http_proxy",
        "socks5h://encoded-user:encoded-password@proxy-host:1080", 0);
    // Supply trusted CA material for HTTPS; never turn verification off merely
    // to compensate for a missing Android trust-store integration.
    ffmpeg.dictSet(options, "ca_file", "/app/private/path/trusted-ca.pem", 0);
    long context = ffmpeg.formatOpenInput(mediaUrl, options);
    // Check context != 0; release it with formatCloseInput when finished.
} finally {
    ffmpeg.dictFree(options);
}
```

`http_proxy` is forwarded to libcurl's `CURLOPT_PROXY`, so it accepts HTTP and
SOCKS proxy URLs. `socks5h` resolves the target hostname at the proxy. Construct
userinfo/IPv6 URLs with proper encoding; do not log credentials.

## Scope and remaining integration work

This change supplies the dependency and build-time checks. It does **not**
replace an application's already shipped `.so`, enable libcurl by default,
modify JNI APIs, or claim that authenticated playback has passed device tests.
The following still belong to application/network integration:

- Detect backend availability before selecting it. The `prefer_libcurl` option
  exists even in builds without the backend; the traditional HTTP implementation
  does not understand SOCKS proxy URLs and can fall back to direct requests.
- Provide Android trust material. The curl build deliberately does not embed
  build-host CA paths; the libcurl FFmpeg backend keeps TLS verification enabled.
  `ca_file` propagation to HLS/DASH child requests must be validated/implemented,
  not inferred from the propagation of `http_proxy` and `prefer_libcurl`.
- Explicit direct/no-proxy behavior: the current libcurl backend skips setting
  `CURLOPT_PROXY` for an empty value, allowing environment proxy defaults.
- Native debug-log redaction, per-session credentials, authentication failures,
  connection/read timeouts, cancellation, redirects, HLS keys/segments and seek.
- Keep `LocalMediaProxyServer` media-loopback connections direct. It is not an
  outbound SOCKS/HTTP proxy and is not changed here.

## Local checks without an Android NDK

```sh
bash -n compat/android/build-libcurl.sh
bash -n compat/android/verify-libcurl.sh
bash compat/android/test-libcurl.sh
# If installed:
actionlint .github/workflows/android.yml
```

The guard tests use fake pkg-config/nm output and cover feature, version,
protocol, static-link and symbol failures. They do not substitute for the
workflow's two Android builds or real proxy/TLS/playback tests.
