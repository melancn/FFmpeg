/*
 * JNI wrapper around FFmpeg for Android.
 *
 * This file exposes a broad, usable slice of the FFmpeg API to Java code in
 * the org.ffmpeg package. It is compiled into libffmpeg_jni.so, which
 * statically links the FFmpeg libraries (avformat, avcodec, avfilter,
 * swscale, swresample, avutil).
 *
 * Design: opaque FFmpeg objects (AVFormatContext*, AVCodecContext*,
 * AVCodec*, AVPacket*, AVFrame*, SwsContext*, SwrContext* and the
 * AVDictionary* "box") are exchanged with Java as 64-bit long handles
 * (0 == null/invalid). Strings are passed as UTF-8. Raw buffers are copied
 * through Java byte[] arguments. No raw native pointers are exposed to
 * Java; callers must size byte[] buffers using the provided sizing helpers
 * (bytesPerSample, swsGetSize est.) or the per-object getters.
 *
 * Every native method here MUST keep the signature declared in
 * org.ffmpeg.FFMpegNative. The JNI symbol name follows
 *   Java_org_ffmpeg_FFMpegNative_<methodName>
 *
 * Copyright (c) 2026 FFmpeg contributors
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <jni.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <android/native_window.h>
#include <android/native_window_jni.h>

#include "libavutil/avutil.h"
#include "libavutil/buffer.h"
#include "libavutil/audio_fifo.h"
#include "libavutil/dict.h"
#include "libavutil/error.h"
#include "libavutil/fifo.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_mediacodec.h"
#include "libavutil/imgutils.h"
#include "libavutil/log.h"
#include <android/log.h>
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/samplefmt.h"
#include "libavutil/channel_layout.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/mediacodec.h"
#include "libavcodec/jni.h"
#include "libavcodec/bsf.h"
#include "libavcodec/codec_par.h"
#include "libavcodec/codec_desc.h"
#include "libavcodec/mpeg4audio.h"
#include "libavcodec/put_bits.h"

#include "libavformat/avformat.h"
#include "libavformat/avio.h"
#include "libavformat/internal.h"
#include "libavformat/demux.h"

#include "libavfilter/avfilter.h"
#include "libavfilter/buffersrc.h"
#include "libavfilter/buffersink.h"

#include "libavutil/mathematics.h"
#include "libavutil/display.h"

#include "libswscale/swscale.h"

#include "libswresample/swresample.h"

/* Optional libass subtitle rendering (see the libass section near the end
 * of this file; compiled only when FFmpeg was configured --enable-libass). */
#include "config.h"
#if CONFIG_LIBASS
#include <ass/ass.h>
#endif

/* Cached for the mediacodec hwdevice free callback, which runs detached from
 * any Java frame and needs AttachCurrentThread to obtain a JNIEnv. */
static JavaVM *g_vm = NULL;

/* Convenience cast from a jlong handle to a typed pointer. */
#define PTR(type, h) ((type)(intptr_t)(h))

static jstring version_to_string(JNIEnv *env, unsigned ver)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%u.%u.%u",
             (ver >> 16) & 0xFF, (ver >> 8) & 0xFF, ver & 0xFF);
    return (*env)->NewStringUTF(env, buf);
}

/* ------------------------------------------------------------------ */
/* Versions / info                                                     */
/* ------------------------------------------------------------------ */

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_getVersion(JNIEnv *env, jobject thiz)
{
    const char *v = av_version_info();
    return (*env)->NewStringUTF(env, v ? v : "unknown");
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_getAvutilVersion(JNIEnv *env, jobject thiz)
{
    return version_to_string(env, avutil_version());
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_getAvcodecVersion(JNIEnv *env, jobject thiz)
{
    return version_to_string(env, avcodec_version());
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_getAvformatVersion(JNIEnv *env, jobject thiz)
{
    return version_to_string(env, avformat_version());
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_getAvfilterVersion(JNIEnv *env, jobject thiz)
{
    return version_to_string(env, avfilter_version());
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_getSwscaleVersion(JNIEnv *env, jobject thiz)
{
    return version_to_string(env, swscale_version());
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_getSwresampleVersion(JNIEnv *env, jobject thiz)
{
    return version_to_string(env, swresample_version());
}

/* ------------------------------------------------------------------ */
/* JNI bridge contract                                                 */
/*                                                                     */
/* getVersion() reports the FFmpeg build. It says nothing about which  */
/* entry points this .so actually implements, so a JAR and a .so can   */
/* disagree while both look healthy. These queries make that failure   */
/* detectable instead of letting callers assume a capability is there. */
/* ------------------------------------------------------------------ */

#define FF_JNI_BRIDGE_API_MAJOR 1
/* 1.1 adds the bounded PCM resampling session. It only appends entry points,
 * so this is a minor bump: a 1.0 caller keeps working unchanged, and a 1.1
 * caller detects the capability bit instead of guessing from the version. */
#define FF_JNI_BRIDGE_API_MINOR 1
#define FF_JNI_BRIDGE_API_VERSION \
    ((FF_JNI_BRIDGE_API_MAJOR << 16) | FF_JNI_BRIDGE_API_MINOR)

#define FF_JNI_CAP_PACKET_BUDGET_INFO    (1LL << 0)
#define FF_JNI_CAP_SIDE_DATA_EXACT_COPY  (1LL << 1)
#define FF_JNI_CAP_BRIDGE_BUILD_INFO     (1LL << 2)
#define FF_JNI_CAP_FRAME_LAYOUT_NORMALIZE (1LL << 3)
/* pcmSessionCreate/Convert/Drain/Reset/Free are implemented, with limits
 * enforced before any allocation or input consumption. */
#define FF_JNI_CAP_BOUNDED_PCM_RESAMPLE  (1LL << 4)

/* Bumped whenever the packet budget accounting rules change, so a stored
 * charge value can be attributed to the policy that produced it. */
#define FF_JNI_PACKET_BUDGET_POLICY_VERSION 1

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_getBridgeApiVersion(JNIEnv *env, jobject thiz)
{
    return FF_JNI_BRIDGE_API_VERSION;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_getBridgeCapabilities(JNIEnv *env, jobject thiz)
{
    return (jlong)(FF_JNI_CAP_PACKET_BUDGET_INFO |
                   FF_JNI_CAP_SIDE_DATA_EXACT_COPY |
                   FF_JNI_CAP_BRIDGE_BUILD_INFO |
                   FF_JNI_CAP_FRAME_LAYOUT_NORMALIZE |
                   FF_JNI_CAP_BOUNDED_PCM_RESAMPLE);
}

/* ABI string for the compiled object, not the device ABI: it identifies
 * which of the shipped libraries is actually loaded. */
#if defined(__aarch64__)
#define FF_JNI_ABI_NAME "arm64-v8a"
#elif defined(__arm__)
#define FF_JNI_ABI_NAME "armeabi-v7a"
#elif defined(__i686__) || defined(__i386__)
#define FF_JNI_ABI_NAME "x86"
#elif defined(__x86_64__)
#define FF_JNI_ABI_NAME "x86_64"
#else
#define FF_JNI_ABI_NAME "unknown"
#endif

#if defined(NDEBUG)
#define FF_JNI_BUILD_TYPE "release"
#else
#define FF_JNI_BUILD_TYPE "debug"
#endif

/* Single-line JSON describing the built library. Deliberately excludes
 * URLs, paths, headers and any other run-time data. */
JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_getBridgeBuildInfo(JNIEnv *env, jobject thiz)
{
    const char *ffver = av_version_info();
    char buf[384];
    snprintf(buf, sizeof(buf),
             "{\"bridgeApi\":%d,\"packetBudgetPolicyVersion\":%d,"
             "\"ffmpegVersion\":\"%s\",\"abi\":\"%s\",\"buildType\":\"%s\"}",
             FF_JNI_BRIDGE_API_VERSION, FF_JNI_PACKET_BUDGET_POLICY_VERSION,
             ffver ? ffver : "unknown", FF_JNI_ABI_NAME, FF_JNI_BUILD_TYPE);
    return (*env)->NewStringUTF(env, buf);
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_strerror(JNIEnv *env, jobject thiz, jint errnum)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    int ret = av_strerror(errnum, buf, sizeof(buf));
    if (ret < 0)
        snprintf(buf, sizeof(buf), "AVERROR %d", (int)errnum);
    return (*env)->NewStringUTF(env, buf);
}

/* ------------------------------------------------------------------ */
/* AVDictionary (stored behind a pointer box so Java keeps a long)     */
/* ------------------------------------------------------------------ */

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_dictCreate(JNIEnv *env, jobject thiz)
{
    AVDictionary **box = (AVDictionary **)calloc(1, sizeof(AVDictionary *));
    return (jlong)(intptr_t)box;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_dictSet(JNIEnv *env, jobject thiz,
                                     jlong box, jstring key, jstring val, jint flags)
{
    AVDictionary **pp = PTR(AVDictionary **, box);
    if (!pp || !key)
        return AVERROR(EINVAL);
    const char *k = (*env)->GetStringUTFChars(env, key, NULL);
    const char *v = val ? (*env)->GetStringUTFChars(env, val, NULL) : NULL;
    int ret = av_dict_set(pp, k, v, flags);
    (*env)->ReleaseStringUTFChars(env, key, k);
    if (val)
        (*env)->ReleaseStringUTFChars(env, val, v);
    return ret;
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_dictGet(JNIEnv *env, jobject thiz,
                                     jlong box, jstring key, jint flags)
{
    AVDictionary **pp = PTR(AVDictionary **, box);
    if (!pp || !key)
        return NULL;
    AVDictionary *d = *pp;
    if (!d)
        return NULL;
    const char *k = (*env)->GetStringUTFChars(env, key, NULL);
    AVDictionaryEntry *e = av_dict_get(d, k, NULL, flags);
    (*env)->ReleaseStringUTFChars(env, key, k);
    return e ? (*env)->NewStringUTF(env, e->value) : NULL;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_dictCount(JNIEnv *env, jobject thiz, jlong box)
{
    AVDictionary **pp = PTR(AVDictionary **, box);
    return pp ? (jint)av_dict_count(*pp) : 0;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_dictFree(JNIEnv *env, jobject thiz, jlong box)
{
    AVDictionary **pp = PTR(AVDictionary **, box);
    if (pp) {
        av_dict_free(pp);
        free(pp);
    }
}

/* ------------------------------------------------------------------ */
/* libavformat                                                         */
/* ------------------------------------------------------------------ */

/* Interrupt plumbing used by both formatOpenInput variants; defined below. */
typedef struct FFInterruptFlag {
    /* Written by the controlling thread, polled by the I/O thread. */
    volatile int aborted;
} FFInterruptFlag;

static int ff_jni_interrupt_cb(void *opaque);

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_formatOpenInput(JNIEnv *env, jobject thiz,
                                             jstring url, jlong optionsBox)
{
    if (!url)
        return 0;
    AVDictionary **opts = optionsBox ? PTR(AVDictionary **, optionsBox) : NULL;
    AVFormatContext *ctx = NULL;
    const char *u = (*env)->GetStringUTFChars(env, url, NULL);
    int ret = avformat_open_input(&ctx, u, NULL, opts);
    (*env)->ReleaseStringUTFChars(env, url, u);
    if (ret < 0 || !ctx)
        return 0;
    return (jlong)(intptr_t)ctx;
}

/* Variant of formatOpenInput whose I/O can be cancelled while it is still
 * running.
 *
 * formatOpenInput() calls avformat_open_input(&ctx=NULL, ...), so the
 * AVFormatContext -- and therefore interrupt_callback -- only exists after
 * the function returns. Nothing the caller does meanwhile (including
 * interruptFlagAbort) can reach the DNS lookup, TLS handshake or HLS
 * playlist reload happening inside, so an abandoned open keeps burning the
 * thread until the protocol's own timeout expires.
 *
 * Here the context is allocated first, the flag is attached to it, and only
 * then is it opened. Raising the flag now makes the in-flight open fail with
 * AVERROR_EXIT promptly.
 *
 * @param url          input URL / path
 * @param optionsBox   AVDictionary** box, 0 for none
 * @param flag         interrupt flag handle; 0 means "no cancellation"
 * @return the opened AVFormatContext, or 0 on failure (same contract as
 *         formatOpenInput). On failure the context is closed here. */
JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_formatOpenInputWithInterrupt(JNIEnv *env, jobject thiz,
                                                          jstring url, jlong optionsBox,
                                                          jlong flag)
{
    if (!url)
        return 0;
    AVDictionary **opts = optionsBox ? PTR(AVDictionary **, optionsBox) : NULL;
    FFInterruptFlag *f = flag ? PTR(FFInterruptFlag *, flag) : NULL;
    /* An already cancelled generation must not start DNS/TLS/probe work. */
    if (ff_jni_interrupt_cb(f))
        return 0;
    AVFormatContext *ctx = avformat_alloc_context();
    if (!ctx)
        return 0;
    if (f) {
        ctx->interrupt_callback.callback = ff_jni_interrupt_cb;
        ctx->interrupt_callback.opaque   = f;
    }
    const char *u = (*env)->GetStringUTFChars(env, url, NULL);
    if (!u) {
        avformat_free_context(ctx);
        return 0;
    }
    int ret = avformat_open_input(&ctx, u, NULL, opts);
    (*env)->ReleaseStringUTFChars(env, url, u);
    if (ret < 0 || !ctx) {
        /* avformat_open_input frees ctx on failure, but free defensively:
         * a NULL return with a non-NULL ctx would otherwise leak. */
        if (ctx)
            avformat_close_input(&ctx);
        return 0;
    }
    return (jlong)(intptr_t)ctx;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_formatFindStreamInfo(JNIEnv *env, jobject thiz, jlong ctx)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c)
        return AVERROR(EINVAL);
    return (jint)avformat_find_stream_info(c, NULL);
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_formatCloseInput(JNIEnv *env, jobject thiz, jlong ctx)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (c)
        avformat_close_input(&c);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_getNbStreams(JNIEnv *env, jobject thiz, jlong ctx)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    return c ? (jint)c->nb_streams : 0;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_getDuration(JNIEnv *env, jobject thiz, jlong ctx)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    return c ? (jlong)c->duration : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_findBestStream(JNIEnv *env, jobject thiz,
                                            jlong ctx, jint type, jint want, jint related)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c)
        return AVERROR(EINVAL);
    return (jint)av_find_best_stream(c, (enum AVMediaType)type, want, related, NULL, 0);
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetStartTime(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c || index < 0 || index >= (jint)c->nb_streams)
        return AV_NOPTS_VALUE;
    return (jlong)c->streams[index]->start_time;
}

static AVCodecParameters *stream_par(AVFormatContext *c, jint index)
{
    if (!c || index < 0 || index >= (jint)c->nb_streams)
        return NULL;
    return c->streams[index]->codecpar;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetCodecId(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVCodecParameters *p = stream_par(PTR(AVFormatContext *, ctx), index);
    return p ? (jint)p->codec_id : AV_CODEC_ID_NONE;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetCodecType(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVCodecParameters *p = stream_par(PTR(AVFormatContext *, ctx), index);
    return p ? (jint)p->codec_type : (jint)AVMEDIA_TYPE_UNKNOWN;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetWidth(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVCodecParameters *p = stream_par(PTR(AVFormatContext *, ctx), index);
    return p ? (jint)p->width : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetHeight(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVCodecParameters *p = stream_par(PTR(AVFormatContext *, ctx), index);
    return p ? (jint)p->height : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetFormat(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVCodecParameters *p = stream_par(PTR(AVFormatContext *, ctx), index);
    return p ? (jint)p->format : -1;
}

/* ------------------------------------------------------------------ */
/* libavcodec                                                          */
/* ------------------------------------------------------------------ */

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_findDecoder(JNIEnv *env, jobject thiz, jint codecId)
{
    const AVCodec *c = avcodec_find_decoder((enum AVCodecID)codecId);
    return (jlong)(intptr_t)c;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_findEncoder(JNIEnv *env, jobject thiz, jint codecId)
{
    const AVCodec *c = avcodec_find_encoder((enum AVCodecID)codecId);
    return (jlong)(intptr_t)c;
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_codecGetName(JNIEnv *env, jobject thiz, jint codecId)
{
    const char *n = avcodec_get_name((enum AVCodecID)codecId);
    return n ? (*env)->NewStringUTF(env, n) : NULL;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_allocCodecContext(JNIEnv *env, jobject thiz, jlong codec)
{
    AVCodec *c = PTR(AVCodec *, codec);
    AVCodecContext *cc = avcodec_alloc_context3(c);
    return (jlong)(intptr_t)cc;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_codecParametersToContext(JNIEnv *env, jobject thiz,
                                                      jlong codecCtx, jlong fmtCtx, jint index)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    AVFormatContext *fc = PTR(AVFormatContext *, fmtCtx);
    AVCodecParameters *p = stream_par(fc, index);
    if (!cc || !p)
        return AVERROR(EINVAL);
    int ret = avcodec_parameters_to_context(cc, p);
    /* avcodec_parameters_to_context() does not carry the stream time base
     * over, and decoders that rescale packet timestamps (mediacodec sends
     * them to the codec in AV_TIME_BASE units) need it. */
    if (ret >= 0)
        cc->pkt_timebase = fc->streams[index]->time_base;
    return (jint)ret;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_codecOpen2(JNIEnv *env, jobject thiz, jlong codecCtx, jlong codec)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    AVCodec *c = PTR(AVCodec *, codec);
    if (!cc)
        return AVERROR(EINVAL);
    return (jint)avcodec_open2(cc, c, NULL);
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_codecFreeContext(JNIEnv *env, jobject thiz, jlong codecCtx)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    if (cc)
        avcodec_free_context(&cc);
}

/* ------------------------------------------------------------------ */
/* MediaCodec hardware decode: bind an Android Surface to the decoder  */
/* so h264/hevc/av1_mediacodec output renders directly to the Surface. */
/*                                                                     */
/* Lifecycle (order matters):                                          */
/*   AVMediaCodecContext *mc = av_mediacodec_alloc_context();          */
/*   av_mediacodec_default_init(cc, mc, surface);  (before open2)      */
/*   avcodec_open2(cc, codec, NULL);                                   */
/*   ... decode ...                                                    */
/*   av_mediacodec_default_free(cc);     (before codecFreeContext)     */
/*   avcodec_free_context(&cc);                                        */
/*                                                                     */
/* av_mediacodec_default_free() frees the AVMediaCodecContext AND the  */
/* surface global ref, and also av_freep(&avctx->hwaccel_context); it  */
/* MUST run before avcodec_free_context() (which does not touch        */
/* hwaccel_context) to avoid a use-after-free.                         */
/* ------------------------------------------------------------------ */

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_mediasurfaceAllocContext(JNIEnv *env, jobject thiz)
{
    return (jlong)(intptr_t)av_mediacodec_alloc_context();
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_mediasurfaceDefaultInit(JNIEnv *env, jobject thiz,
                                                     jlong codecCtx, jlong mcCtx, jobject surface)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    AVMediaCodecContext *mc = PTR(AVMediaCodecContext *, mcCtx);
    if (!cc || !mc || !surface)
        return AVERROR(EINVAL);
    /* av_mediacodec_default_init NewGlobalRefs the surface and installs mc
     * into avctx->hwaccel_context. Must run before codecOpen2. */
    return (jint)av_mediacodec_default_init(cc, mc, (void *)surface);
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_mediasurfaceDefaultFree(JNIEnv *env, jobject thiz, jlong codecCtx)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    if (cc)
        av_mediacodec_default_free(cc);
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_mediasurfaceFree(JNIEnv *env, jobject thiz, jlong mcCtx)
{
    AVMediaCodecContext *mc = PTR(AVMediaCodecContext *, mcCtx);
    if (mc)
        av_freep(&mc);
}

/* ------------------------------------------------------------------ */
/* MediaCodec Surface zero-copy via AV_HWDEVICE_TYPE_MEDIACODEC.       */
/*                                                                     */
/* The surface-binding path used above (mediasurfaceDefaultInit) stores */
/* the Surface in an ad-hoc AVMediaCodecContext hung off                */
/* avctx->hwaccel_context. Under the default get_format that branch is */
/* unreachable for the mediacodec decoder (its hw_config declares       */
/* AD_HOC|HW_DEVICE_CTX, not INTERNAL), so the decoder falls back to    */
/* software output (NV12 = 23). To hit the zero-copy path we instead    */
/* build an AV_HWDEVICE_TYPE_MEDIACODEC device whose hwctx->surface is  */
/* the App Surface, and attach it as avctx->hw_device_ctx BEFORE        */
/* codecOpen2. This satisfies default get_format (returns               */
/* AV_PIX_FMT_MEDIACODEC) and the decoder's surface lookup (reads       */
/* device_ctx->hwctx->surface), so frames come back with format 164 and */
/* frame->data[3] = AVMediaCodecBuffer* ready for                      */
/* av_mediacodec_render_buffer_at_time.                                 */
/* ------------------------------------------------------------------ */

/* av_hwdevice_ctx_free calls ctx->free BEFORE av_freep(&ctx->hwctx)
 * (hwcontext.c), so inside the callback hwctx is still valid and we can
 * safely pull the Surface global ref back out and delete it. The free
 * callback runs detached from any Java frame, so use the JavaVM cached
 * in JNI_OnLoad to obtain a JNIEnv (AttachCurrentThread if needed). */
static void mediacodec_hwdevice_free(AVHWDeviceContext *ctx)
{
    if (!ctx || !ctx->hwctx || !g_vm)
        return;
    AVMediaCodecDeviceContext *mc = (AVMediaCodecDeviceContext *)ctx->hwctx;
    if (!mc->surface)
        return;
    JNIEnv *env = NULL;
    int attached = 0;
    if ((*g_vm)->GetEnv(g_vm, (void **)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) == JNI_OK)
            attached = 1;
        else
            env = NULL;
    }
    if (env)
        (*env)->DeleteGlobalRef(env, (jobject)mc->surface);
    if (attached)
        (*g_vm)->DetachCurrentThread(g_vm);
    mc->surface = NULL;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_mediasurfaceHwdeviceCreate(JNIEnv *env, jobject thiz, jobject surface)
{
    if (!surface)
        return 0;
    AVBufferRef *ref = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_MEDIACODEC);
    if (!ref)
        return 0;
    AVHWDeviceContext *ctx = (AVHWDeviceContext *)ref->data;
    AVMediaCodecDeviceContext *mc = (AVMediaCodecDeviceContext *)ctx->hwctx;
    jobject gref = (*env)->NewGlobalRef(env, surface);
    if (!gref) {
        av_buffer_unref(&ref);
        return 0;
    }
    mc->surface = (void *)gref;
    mc->native_window = NULL;
    mc->create_window = 0;
    ctx->free = mediacodec_hwdevice_free;
    if (av_hwdevice_ctx_init(ref) < 0) {
        (*env)->DeleteGlobalRef(env, gref);
        mc->surface = NULL;
        av_buffer_unref(&ref);
        return 0;
    }
    return (jlong)(intptr_t)ref;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_getContextWidth(JNIEnv *env, jobject thiz, jlong codecCtx)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    return cc ? (jint)cc->width : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_getContextHeight(JNIEnv *env, jobject thiz, jlong codecCtx)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    return cc ? (jint)cc->height : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_getContextPixFmt(JNIEnv *env, jobject thiz, jlong codecCtx)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    return cc ? (jint)cc->pix_fmt : -1;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_getContextCodecId(JNIEnv *env, jobject thiz, jlong codecCtx)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    return cc ? (jint)cc->codec_id : AV_CODEC_ID_NONE;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_codecSendPacket(JNIEnv *env, jobject thiz, jlong codecCtx, jlong pkt)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    AVPacket *p = pkt ? PTR(AVPacket *, pkt) : NULL;
    if (!cc)
        return AVERROR(EINVAL);
    return (jint)avcodec_send_packet(cc, p);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_codecReceiveFrame(JNIEnv *env, jobject thiz, jlong codecCtx, jlong frame)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    AVFrame *f = PTR(AVFrame *, frame);
    if (!cc || !f)
        return AVERROR(EINVAL);
    return (jint)avcodec_receive_frame(cc, f);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_codecSendFrame(JNIEnv *env, jobject thiz, jlong codecCtx, jlong frame)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    AVFrame *f = frame ? PTR(AVFrame *, frame) : NULL;
    if (!cc)
        return AVERROR(EINVAL);
    return (jint)avcodec_send_frame(cc, f);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_codecReceivePacket(JNIEnv *env, jobject thiz, jlong codecCtx, jlong pkt)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    AVPacket *p = PTR(AVPacket *, pkt);
    if (!cc || !p)
        return AVERROR(EINVAL);
    return (jint)avcodec_receive_packet(cc, p);
}

/* ------------------------------------------------------------------ */
/* AVPacket                                                            */
/* ------------------------------------------------------------------ */

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_packetAlloc(JNIEnv *env, jobject thiz)
{
    return (jlong)(intptr_t)av_packet_alloc();
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_packetFree(JNIEnv *env, jobject thiz, jlong pkt)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    if (p)
        av_packet_free(&p);
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_packetClone(JNIEnv *env, jobject thiz, jlong pkt)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    if (!p)
        return 0;
    return (jlong)(intptr_t)av_packet_clone(p);
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_packetUnref(JNIEnv *env, jobject thiz, jlong pkt)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    if (p)
        av_packet_unref(p);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_packetGetSize(JNIEnv *env, jobject thiz, jlong pkt)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    return p ? (jint)p->size : 0;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_packetGetPts(JNIEnv *env, jobject thiz, jlong pkt)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    return p ? (jlong)p->pts : AV_NOPTS_VALUE;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_packetGetDts(JNIEnv *env, jobject thiz, jlong pkt)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    return p ? (jlong)p->dts : AV_NOPTS_VALUE;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_packetGetStreamIndex(JNIEnv *env, jobject thiz, jlong pkt)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    return p ? (jint)p->stream_index : -1;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_packetGetFlags(JNIEnv *env, jobject thiz, jlong pkt)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    return p ? (jint)p->flags : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_packetCopyData(JNIEnv *env, jobject thiz,
                                            jlong pkt, jbyteArray out, jint off, jint len)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    if (!p || !out || off < 0 || len < 0)
        return AVERROR(EINVAL);

    /* Validate the destination range before touching the array: an
     * unchecked SetByteArrayRegion past the end throws
     * ArrayIndexOutOfBoundsException, which is not a usable contract. Use
     * subtraction so off + len cannot overflow. */
    const jsize out_len = (*env)->GetArrayLength(env, out);
    if (off > out_len || len > out_len - off)
        return AVERROR(EINVAL);

    /* No payload is not an error: report zero bytes copied so callers can
     * distinguish it from a rejected call. */
    if (p->size <= 0 || !p->data || len == 0)
        return 0;

    const jint n = (p->size < len) ? (jint)p->size : len;
    (*env)->SetByteArrayRegion(env, out, off, n, (const jbyte *)p->data);
    /* Keep a pending JVM exception pending; do not report success for a
     * copy the VM refused to perform. */
    if ((*env)->ExceptionCheck(env))
        return AVERROR(ENOMEM);
    return n;
}

/* ------------------------------------------------------------------ */
/* Packet memory budget                                                */
/*                                                                     */
/* The returned charge is an admission-control estimate, NOT the exact  */
/* malloc size and NOT process RSS. Shared buffers are charged once per */
/* holding packet, so totals may over-count but never under-count. It   */
/* says nothing about demuxer-internal buffering, decoders, swresample  */
/* or libass.                                                           */
/* ------------------------------------------------------------------ */

/* Conservative per-object overhead allowance for allocator bookkeeping and
 * the padding FFmpeg requires around buffers. This is policy, not a measured
 * size; bump FF_JNI_PACKET_BUDGET_POLICY_VERSION when it changes. */
#define FF_JNI_BUDGET_METADATA_ALLOWANCE 256

static int ff_jni_budget_add(uint64_t *total, uint64_t value)
{
    const uint64_t limit = (uint64_t)INT64_MAX;
    if (value > limit || *total > limit - value)
        return AVERROR(EOVERFLOW);
    *total += value;
    return 0;
}

/* Number of values written by ff_jni_packet_budget(); frozen for API major 1
 * and mirrored by FFMpegNative.PACKET_BUDGET_INFO_SLOTS. */
#define FF_JNI_PACKET_BUDGET_SLOTS 6

/* Bytes occupied by count samples of the given format and channel count.
 *
 * Java passes sample *counts* as jint, and the size is
 * count * bytes_per_sample * channels. Computing that in int (as swrConvert
 * used to) wraps for perfectly expressible requests -- 2^28 S16 stereo samples
 * is 1 GiB, which fits in int64 but not in int -- and a wrapped "capacity" is
 * then indistinguishable from a real one. The product is therefore formed in
 * 64 bits with an explicit bound check at each step.
 *
 * Planar and interleaved audio occupy the same total number of bytes, so one
 * formula covers both. Returns the byte count, or a negative AVERROR on
 * invalid arguments or overflow. */
static int64_t ff_jni_audio_bytes(int64_t count, int fmt, int channels)
{
    const int bps = av_get_bytes_per_sample((enum AVSampleFormat)fmt);
    const int64_t limit = INT64_MAX;

    /* Divide-by-zero is impossible past this point: bps and channels are
     * both >= 1, so the two divisions below are always well defined. */
    if (count < 0 || bps <= 0 || channels <= 0)
        return AVERROR(EINVAL);
    if (count > limit / bps)
        return AVERROR(EOVERFLOW);
    if (count * bps > limit / channels)
        return AVERROR(EOVERFLOW);
    return count * bps * channels;
}

/* Channel count implied by a layout mask. 0 means "the mask does not describe
 * a usable layout", which callers treat as "cannot size" rather than
 * guessing a count. */
static int ff_jni_mask_channels(uint64_t mask)
{
    AVChannelLayout l = { 0 };
    int ch;

    if (!mask)
        return 0;
    if (av_channel_layout_from_mask(&l, mask) < 0)
        return 0;
    ch = l.nb_channels;
    av_channel_layout_uninit(&l);
    return ch;
}

/* Fill out[0..FF_JNI_PACKET_BUDGET_SLOTS-1] for a packet. Split from the JNI
 * wrapper so it can be exercised without a JVM. Returns 0 or a negative
 * AVERROR; on failure the caller must not publish the array. */
static int ff_jni_packet_budget(const AVPacket *packet,
                                int64_t out[FF_JNI_PACKET_BUDGET_SLOTS])
{
    uint64_t charge = sizeof(AVPacket);
    uint64_t side_bytes = 0;
    uint64_t payload_capacity = 0;
    uint64_t opaque_capacity = 0;
    int ret;
    int i;

    if (!packet ||
        packet->size < 0 ||
        packet->side_data_elems < 0 ||
        (packet->size > 0 && !packet->data) ||
        (packet->side_data_elems > 0 && !packet->side_data))
        return AVERROR(EINVAL);

    if ((ret = ff_jni_budget_add(&charge, FF_JNI_BUDGET_METADATA_ALLOWANCE)) < 0)
        return ret;

    if (packet->buf) {
        payload_capacity = (uint64_t)packet->buf->size;
        /* A payload larger than its own buffer means a corrupt packet. */
        if (packet->buf->size < 0 ||
            payload_capacity < (uint64_t)packet->size)
            return AVERROR(EINVAL);
    } else if (packet->data || packet->size > 0) {
        /* Without a reference, a clone must duplicate the payload, so charge
         * the size it would occupy including FFmpeg's required padding. */
        payload_capacity = (uint64_t)packet->size + AV_INPUT_BUFFER_PADDING_SIZE;
    }

    if (payload_capacity) {
        if ((ret = ff_jni_budget_add(&charge, payload_capacity)) < 0)
            return ret;
        if ((ret = ff_jni_budget_add(&charge,
                                     sizeof(AVBufferRef) +
                                     FF_JNI_BUDGET_METADATA_ALLOWANCE)) < 0)
            return ret;
    }

    /* Every side data entry counts, of any type: a packet carrying only
     * dynamic/HDR/subtitle metadata is still resident memory. */
    if ((ret = ff_jni_budget_add(&charge,
                                 (uint64_t)packet->side_data_elems *
                                 sizeof(AVPacketSideData))) < 0)
        return ret;

    for (i = 0; i < packet->side_data_elems; i++) {
        const AVPacketSideData *sd = &packet->side_data[i];
        if (sd->size < 0 || (sd->size > 0 && !sd->data))
            return AVERROR(EINVAL);
        if ((ret = ff_jni_budget_add(&side_bytes, (uint64_t)sd->size)) < 0)
            return ret;
        if ((ret = ff_jni_budget_add(&charge, (uint64_t)sd->size)) < 0)
            return ret;
        if ((ret = ff_jni_budget_add(&charge,
                                     AV_INPUT_BUFFER_PADDING_SIZE)) < 0)
            return ret;
    }

    /* opaque_ref is owned and its buffer is real memory. opaque (the plain
     * pointer) is not owned by the packet, so it is deliberately not
     * dereferenced or charged. */
    if (packet->opaque_ref) {
        if (packet->opaque_ref->size < 0)
            return AVERROR(EINVAL);
        opaque_capacity = (uint64_t)packet->opaque_ref->size;
        if ((ret = ff_jni_budget_add(&charge, opaque_capacity)) < 0)
            return ret;
        if ((ret = ff_jni_budget_add(&charge,
                                     sizeof(AVBufferRef) +
                                     FF_JNI_BUDGET_METADATA_ALLOWANCE)) < 0)
            return ret;
    }

    out[0] = (int64_t)packet->size;
    out[1] = (int64_t)payload_capacity;
    out[2] = (int64_t)side_bytes;
    out[3] = (int64_t)packet->side_data_elems;
    out[4] = (int64_t)opaque_capacity;
    out[5] = (int64_t)charge;
    return 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_packetGetBudgetInfo(JNIEnv *env, jobject thiz,
                                                 jlong pkt, jlongArray out)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    int64_t values[FF_JNI_PACKET_BUDGET_SLOTS];
    int ret;

    if (!out || !p)
        return AVERROR(EINVAL);
    /* Reject undersized arrays instead of writing only part of the layout:
     * a partially filled snapshot would look valid to the caller. */
    if ((*env)->GetArrayLength(env, out) < FF_JNI_PACKET_BUDGET_SLOTS)
        return AVERROR(EINVAL);

    ret = ff_jni_packet_budget(p, values);
    if (ret < 0)
        return ret;

    (*env)->SetLongArrayRegion(env, out, 0, FF_JNI_PACKET_BUDGET_SLOTS,
                               (const jlong *)values);
    if ((*env)->ExceptionCheck(env))
        return AVERROR(ENOMEM);
    return 0;
}

/* Shared lookup for the two exact side-data queries. */
static const AVPacketSideData *ff_jni_find_side_data(const AVPacket *p, int type)
{
    int i;
    if (!p || p->side_data_elems < 0 ||
        (p->side_data_elems > 0 && !p->side_data))
        return NULL;
    for (i = 0; i < p->side_data_elems; i++) {
        if ((int)p->side_data[i].type == type) {
            /* An entry that claims content but has no pointer is corrupt;
             * treat it as absent rather than dereferencing NULL. */
            if (p->side_data[i].size < 0 ||
                (p->side_data[i].size > 0 && !p->side_data[i].data))
                return NULL;
            return &p->side_data[i];
        }
    }
    return NULL;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_packetGetSideDataSize(JNIEnv *env, jobject thiz,
                                                   jlong pkt, jint type)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    const AVPacketSideData *sd;

    if (!p)
        return AVERROR(EINVAL);
    sd = ff_jni_find_side_data(p, type);
    if (!sd)
        return AVERROR(ENOENT);
    return (jint)sd->size;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_packetCopySideDataExact(JNIEnv *env, jobject thiz,
                                                     jlong pkt, jint type,
                                                     jbyteArray out,
                                                     jint offset, jint capacity)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    const AVPacketSideData *sd;
    jsize out_len;

    if (!p || !out || offset < 0 || capacity < 0)
        return AVERROR(EINVAL);

    /* Subtraction form: offset + capacity could overflow. */
    out_len = (*env)->GetArrayLength(env, out);
    if (offset > out_len || capacity > out_len - offset)
        return AVERROR(EINVAL);

    sd = ff_jni_find_side_data(p, type);
    if (!sd)
        return AVERROR(ENOENT);

    /* Never truncate: refuse instead of copying a prefix and reporting it
     * as the whole entry. */
    if ((uint64_t)sd->size > (uint64_t)capacity)
        return AVERROR(ENOSPC);

    if (sd->size > 0) {
        (*env)->SetByteArrayRegion(env, out, offset,
                                   (jsize)sd->size,
                                   (const jbyte *)sd->data);
        if ((*env)->ExceptionCheck(env))
            return AVERROR(ENOMEM);
    }
    return (jint)sd->size;
}

/* ------------------------------------------------------------------ */
/* AVFrame                                                             */
/* ------------------------------------------------------------------ */

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_frameAlloc(JNIEnv *env, jobject thiz)
{
    return (jlong)(intptr_t)av_frame_alloc();
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_frameFree(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    if (f)
        av_frame_free(&f);
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_frameUnref(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    if (f)
        av_frame_unref(f);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetWidth(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    return f ? (jint)f->width : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetHeight(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    return f ? (jint)f->height : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetFormat(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    return f ? (jint)f->format : -1;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetPts(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    return f ? (jlong)f->pts : AV_NOPTS_VALUE;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetBestEffortTimestamp(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    return f ? (jlong)f->best_effort_timestamp : AV_NOPTS_VALUE;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetPktDts(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    return f ? (jlong)f->pkt_dts : AV_NOPTS_VALUE;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameIsKeyFrame(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    return (f && (f->flags & AV_FRAME_FLAG_KEY)) ? 1 : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetLineSize(JNIEnv *env, jobject thiz, jlong frame, jint plane)
{
    AVFrame *f = PTR(AVFrame *, frame);
    if (!f || plane < 0 || plane >= AV_NUM_DATA_POINTERS)
        return 0;
    return (jint)f->linesize[plane];
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetNbSamples(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    return f ? (jint)f->nb_samples : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetChannels(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    return f ? (jint)f->ch_layout.nb_channels : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetSampleRate(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    return f ? (jint)f->sample_rate : 0;
}

/* Write a definite channel layout into frame->ch_layout when the decoder left
 * it as AV_CHANNEL_ORDER_UNSPEC, and change nothing otherwise.
 *
 * Why this exists: swr_init() resolves the user input layout against
 * used_ch_layout and stores a definite order (swresample.c: av_opt_set_chlayout
 * -> s->in_ch_layout is replaced by s->used_ch_layout when it was UNSPEC).
 * swr_convert_frame() -> config_changed() then compares that stored layout with
 * each incoming frame's layout through av_channel_layout_compare(), which
 * returns "different" whenever exactly one side is UNSPEC. So the first frame
 * initialises the resampler, and every following UNSPEC frame reports
 * AVERROR_INPUT_CHANGED. The usual reaction -- rebuild the resampler -- closes
 * it and drops the tail samples still buffered inside it. Making the order
 * definite once, before the first convert, keeps the layout stable for the whole
 * stream and is idempotent: a frame that already carries a definite layout is
 * not touched, so wiring this in cannot change the behaviour of existing
 * streams.
 *
 * declaredMask is the AVChannelLayout bitmask obtained from the stream's
 * codecpar (streamGetParChannelLayout: a mask value, NOT a pointer, with 0
 * meaning "no usable declaration"). It is read, never retained or freed.
 *
 * Return value:
 *    0  layout was already definite (and valid), nothing was modified
 *   >0  layout was normalised, the value is the channel count now in effect
 *  <0  AVERROR: NULL frame, invalid/unsupported layout, or an UNSPEC layout
 *      with more than 2 channels that cannot be inferred without guessing
 *
 * More than two channels with no declaration are rejected on purpose: there is
 * no safe default for, say, 6 channels (5.1, 5.1(side) and hexa all have six)
 * and a wrong guess silently mis-maps every later sample. Failing lets the
 * caller decide between an error and continuing without conversion.
 *
 * Note on the mono/stereo case: av_channel_layout_default() is a convention
 * (1 -> FRONT_CENTER, 2 -> FL|FR), not a fact read from the stream. It matches
 * real mono/stereo PCM, and for a single channel the position is irrelevant.
 * It is also exactly what swr_init() itself falls back to for input layouts it
 * has to resolve (used_ch_layout -> av_channel_layout_default), so this choice
 * makes the frame agree with the resampler rather than overriding it.
 *
 * Frames must not be shared at this point: the layout is modified in place.
 * Call it on a frame the caller owns exclusively (a freshly decoded frame
 * before it is handed to libswresample). */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameNormalizeChannelLayout(JNIEnv *env, jobject thiz,
                                                         jlong frame, jlong declaredMask)
{
    AVFrame *f = PTR(AVFrame *, frame);
    AVChannelLayout *in;
    AVChannelLayout decl = { 0 }, next = { 0 };
    int ret;

    if (!f)
        return AVERROR(EINVAL);

    in = &f->ch_layout;
    /* Already definite: report "nothing to do" and leave the frame intact.
     * Checking the layout instead of trusting it keeps a corrupt order from
     * reaching swr, but a *valid* definite layout is never rewritten. */
    if (in->order != AV_CHANNEL_ORDER_UNSPEC) {
        if (!av_channel_layout_check(in))
            return AVERROR(EINVAL);
        return 0;
    }
    /* An UNSPEC layout is expected to carry only a channel count. */
    if (in->nb_channels <= 0 || in->nb_channels > 64)
        return AVERROR(EINVAL);

    /* Prefer the container's declaration, but only when its channel count
     * matches the frame's: a declaration describing a different channel count
     * describes a different stream and would cement a wrong mapping. The mask
     * is a value, so it is rebuilt here and checked rather than cast. */
    ret = (declaredMask != 0)
          ? av_channel_layout_from_mask(&decl, (uint64_t)declaredMask)
          : AVERROR(EINVAL);
    if (ret >= 0 && decl.nb_channels == in->nb_channels) {
        if (av_channel_layout_copy(&next, &decl) < 0) {
            av_channel_layout_uninit(&decl);
            return AVERROR(ENOMEM);
        }
    } else {
        if (in->nb_channels > 2) {
            av_channel_layout_uninit(&decl);
            return AVERROR(EINVAL);
        }
        /* av_channel_layout_default() returns void and, for a channel count
         * with no standard layout, leaves the order as UNSPEC rather than
         * failing. For 1 and 2 it always installs a NATIVE layout, but verify
         * instead of assuming: installing another UNSPEC would look like
         * success while leaving the resampler with the layout that caused the
         * problem. */
        av_channel_layout_default(&next, in->nb_channels);
        if (next.order != AV_CHANNEL_ORDER_NATIVE ||
            next.nb_channels != in->nb_channels) {
            av_channel_layout_uninit(&next);
            av_channel_layout_uninit(&decl);
            return AVERROR(EINVAL);
        }
    }
    av_channel_layout_uninit(&decl);

    av_channel_layout_uninit(in);
    *in = next;              /* ownership of any allocated map moves to frame */
    return (jint)in->nb_channels;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetSampleFormat(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    return f ? (jint)f->format : -1;
}

/* Copy a full decoded video frame into a flat byte buffer (all planes,
 * linesize-aligned to a row of width*bpp). The buffer must be at least
 * frameGetVideoSize() bytes long; use swsGetBufferBytes() helper or size
 * as width*height*4 when converting to AV_PIX_FMT_RGBA. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameCopyVideo(JNIEnv *env, jobject thiz,
                                            jlong frame, jbyteArray out, jint off, jint len)
{
    AVFrame *f = PTR(AVFrame *, frame);
    if (!f || !out || f->width <= 0 || f->height <= 0 || f->format < 0)
        return AVERROR(EINVAL);
    int needed = av_image_get_buffer_size((enum AVPixelFormat)f->format,
                                          f->width, f->height, 1);
    if (needed < 0)
        return needed;
    if (len < needed)
        return AVERROR(ENOMEM);
    uint8_t *buf = (uint8_t *)malloc(needed);
    if (!buf)
        return AVERROR(ENOMEM);
    int ret = av_image_copy_to_buffer(buf, needed,
                                      (const uint8_t *const *)f->data,
                                      (const int *)f->linesize,
                                      (enum AVPixelFormat)f->format,
                                      f->width, f->height, 1);
    if (ret >= 0) {
        (*env)->SetByteArrayRegion(env, out, off, needed, (const jbyte *)buf);
        ret = needed;
    }
    free(buf);
    return ret;
}

/* Copy decoded audio frame bytes. For interleaved (non-planar) formats all
 * channels are copied from data[0]; for planar formats only plane 0 is copied
 * (use frameCopyAudioPlane for individual channels). Returns bytes copied, or
 * a negative AVERROR.
 *
 * The copied size is the frame's *actual* sample data size, not linesize[0]:
 * av_frame_get_buffer pads linesize for SIMD alignment, so a caller sizing its
 * array as nb_samples*channels*bps (the true payload) would otherwise get
 * ENOMEM. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameCopyAudio(JNIEnv *env, jobject thiz,
                                            jlong frame, jbyteArray out, jint off, jint len)
{
    AVFrame *f = PTR(AVFrame *, frame);
    if (!f || !out)
        return AVERROR(EINVAL);
    enum AVSampleFormat fmt = (enum AVSampleFormat)f->format;
    int ch = f->ch_layout.nb_channels;
    int bps = av_get_bytes_per_sample(fmt);
    if (f->nb_samples <= 0 || ch <= 0 || bps <= 0)
        return AVERROR(EINVAL);
    int total = av_sample_fmt_is_planar(fmt)
        ? f->nb_samples * bps
        : f->nb_samples * ch * bps;
    /* Never read past the allocated plane. */
    if (f->linesize[0] > 0 && total > f->linesize[0])
        total = f->linesize[0];
    if (total <= 0)
        return AVERROR(EINVAL);
    if (len < total)
        return AVERROR(ENOMEM);
    const uint8_t *src = f->data[0];
    (*env)->SetByteArrayRegion(env, out, off, total, (const jbyte *)src);
    return total;
}

/* ------------------------------------------------------------------ */
/* MediaCodec output buffer rendering.                                 */
/*                                                                     */
/* On the MediaCodec hardware path the decoded AVFrame carries an      */
/* opaque AVMediaCodecBuffer* in frame->data[3]; frame->buf[0] owns the */
/* reference count. After the Java layer is done with the buffer it     */
/* must call one of the render/release entry points below, THEN         */
/* frameUnref(frame) to recycle the AVFrame and release the underlying  */
/* MediaCodec buffer memory.                                            */
/*                                                                     */
/* Correct per-frame order:                                            */
/*   codecReceiveFrame -> mediasurfaceGetBuffer (non-0) ->             */
/*   mediasurfaceRenderBufferAtTime | mediasurfaceReleaseBuffer(1) ->  */
/*   frameUnref(frame)                                                 */
/*                                                                     */
/* On a software decode path frame->data[3] is NULL, so                 */
/* mediasurfaceGetBuffer returns 0 and the render helpers error out;    */
/* use the normal frameCopyVideo path there. HW frames must NOT be      */
/* passed to frameCopyVideo (data[0..2] are NULL).                      */
/* ------------------------------------------------------------------ */

/* Hardware output bookkeeping.
 *
 * The MediaCodec path has no session handle to attach state to, and
 * MediaCodecDecContext (where hw_buffer_count lives) is private to
 * libavcodec. So the bridge keeps its own registry of buffers it handed to
 * Java and has not yet seen released. That registry is the evidence that
 * "the frame was returned" -- the return value of the release call is not,
 * because av_mediacodec_release_buffer() still defers to
 * ff_AMediaCodec_releaseOutputBuffer() only when the buffer's serial is
 * current (mediacodec.c:91-100).
 *
 * Entries are recorded by buffer pointer, which is stable for the lifetime
 * of an AVMediaCodecBuffer because frame->buf[0] owns it. */
typedef struct FFHwOutputReg {
    pthread_mutex_t    lock;
    int                outstanding;
} FFHwOutputReg;

static FFHwOutputReg g_hw_output = { PTHREAD_MUTEX_INITIALIZER, 0 };

/* Record that a buffer is now in Java's hands. Idempotent per call: the
 * caller gets one release per get, so one increment per get is right. */
static void ff_hw_output_acquire(void)
{
    pthread_mutex_lock(&g_hw_output.lock);
    g_hw_output.outstanding++;
    pthread_mutex_unlock(&g_hw_output.lock);
}

static void ff_hw_output_release(void)
{
    pthread_mutex_lock(&g_hw_output.lock);
    if (g_hw_output.outstanding > 0)
        g_hw_output.outstanding--;
    pthread_mutex_unlock(&g_hw_output.lock);
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_mediasurfaceGetBuffer(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    if (!f || f->format != AV_PIX_FMT_MEDIACODEC || !f->data[3])
        return 0;
    ff_hw_output_acquire();
    return (jlong)(intptr_t)f->data[3];
}

/* The release is the moment the buffer leaves Java's hands, so the registry
 * is decremented only when the call actually reported success: a failed
 * release means the buffer was NOT returned, and pretending otherwise would
 * let a detach claim completeness it does not have. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_mediasurfaceReleaseBuffer(JNIEnv *env, jobject thiz,
                                                       jlong buf, jint render)
{
    AVMediaCodecBuffer *b = PTR(AVMediaCodecBuffer *, buf);
    int ret;

    if (!b)
        return AVERROR(EINVAL);
    ret = (jint)av_mediacodec_release_buffer(b, render);
    if (ret >= 0)
        ff_hw_output_release();
    return ret;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_mediasurfaceRenderBufferAtTime(JNIEnv *env, jobject thiz,
                                                            jlong buf, jlong nanoTime)
{
    AVMediaCodecBuffer *b = PTR(AVMediaCodecBuffer *, buf);
    int ret;

    if (!b)
        return AVERROR(EINVAL);
    ret = (jint)av_mediacodec_render_buffer_at_time(b, (int64_t)nanoTime);
    if (ret >= 0)
        ff_hw_output_release();
    return ret;
}

/* How many hardware buffers this bridge handed out and has not seen
 * released. A non-zero value means the producer cannot be considered
 * detached: the Surface may still be referenced by those buffers. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_mediasurfaceGetOutstandingBuffers(JNIEnv *env, jobject thiz)
{
    int n;

    pthread_mutex_lock(&g_hw_output.lock);
    n = g_hw_output.outstanding;
    pthread_mutex_unlock(&g_hw_output.lock);
    return n;
}

/* ------------------------------------------------------------------ */
/* libswscale                                                          */
/* ------------------------------------------------------------------ */

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_swsGetContext(JNIEnv *env, jobject thiz,
                                           jint srcW, jint srcH, jint srcFmt,
                                           jint dstW, jint dstH, jint dstFmt, jint flags)
{
    SwsContext *c = sws_getContext(srcW, srcH, (enum AVPixelFormat)srcFmt,
                                  dstW, dstH, (enum AVPixelFormat)dstFmt, flags,
                                  NULL, NULL, NULL);
    return (jlong)(intptr_t)c;
}

/* Scale srcFrame into out (a Java byte[]) at dstWxdstH/dstFmt. Returns
 * bytes written to out, or a negative AVERROR. Size out appropriately:
 * for AV_PIX_FMT_RGBA use dstW*dstH*4; call swsGetBufferBytes for others. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_swsScaleFrame(JNIEnv *env, jobject thiz,
                                           jlong sws, jlong srcFrame,
                                           jint dstW, jint dstH, jint dstFmt,
                                           jbyteArray out, jint off, jint len)
{
    struct SwsContext *s = PTR(struct SwsContext *, sws);
    AVFrame *sf = PTR(AVFrame *, srcFrame);
    if (!s || !sf || !out || dstW <= 0 || dstH <= 0)
        return AVERROR(EINVAL);

    uint8_t *dst_data[4] = {0};
    int dst_linesize[4] = {0};
    int alloc = av_image_alloc(dst_data, dst_linesize, dstW, dstH,
                               (enum AVPixelFormat)dstFmt, 1);
    if (alloc < 0)
        return alloc;

    const uint8_t *src_data[4];
    for (int i = 0; i < 4; i++)
        src_data[i] = sf->data[i];

    sws_scale(s, src_data, sf->linesize, 0, sf->height, dst_data, dst_linesize);

    int needed = av_image_get_buffer_size((enum AVPixelFormat)dstFmt, dstW, dstH, 1);
    int ret;
    if (needed < 0) {
        ret = needed;
    } else if (len < needed) {
        ret = AVERROR(ENOMEM);
    } else {
        uint8_t *buf = (uint8_t *)malloc(needed);
        if (!buf) {
            ret = AVERROR(ENOMEM);
        } else {
            av_image_copy_to_buffer(buf, needed,
                                    (const uint8_t *const *)dst_data,
                                    (const int *)dst_linesize,
                                    (enum AVPixelFormat)dstFmt, dstW, dstH, 1);
            (*env)->SetByteArrayRegion(env, out, off, needed, (const jbyte *)buf);
            free(buf);
            ret = needed;
        }
    }
    av_freep(&dst_data[0]);
    return ret;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_swsFreeContext(JNIEnv *env, jobject thiz, jlong sws)
{
    struct SwsContext *s = PTR(struct SwsContext *, sws);
    if (s)
        sws_freeContext(s);
}

/* ------------------------------------------------------------------ */
/* libswresample                                                       */
/* ------------------------------------------------------------------ */

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_swrAlloc(JNIEnv *env, jobject thiz)
{
    return (jlong)(intptr_t)swr_alloc();
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_swrSetOpts(JNIEnv *env, jobject thiz, jlong ctx,
                                        jint outFmt, jint outRate, jlong outLayout,
                                        jint inFmt,  jint inRate,  jlong inLayout,
                                        jint logLevel)
{
    struct SwrContext *c = PTR(struct SwrContext *, ctx);
    if (!c)
        return AVERROR(EINVAL);
    av_opt_set_int(c, "out_sample_fmt",     outFmt,    0);
    av_opt_set_int(c, "out_sample_rate",    outRate,   0);
    av_opt_set_int(c, "out_channel_layout", outLayout, 0);
    av_opt_set_int(c, "in_sample_fmt",      inFmt,     0);
    av_opt_set_int(c, "in_sample_rate",     inRate,    0);
    av_opt_set_int(c, "in_channel_layout",  inLayout,  0);
    av_opt_set_int(c, "log_level_offset",   logLevel,  0);
    return 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_swrInit(JNIEnv *env, jobject thiz, jlong ctx)
{
    struct SwrContext *c = PTR(struct SwrContext *, ctx);
    return c ? (jint)swr_init(c) : AVERROR(EINVAL);
}

/* Capacity check for swrConvert(), split out so the limit can be tested
 * without a JVM.
 *
 * outCount/inCount are sample counts supplied by Java; outCapacity is the
 * real length of the out array as reported by GetArrayLength(). The old code
 * computed out_bytes = outCount * bps * channels in int and then never
 * compared it to anything, so swr_convert() was handed outCount unchecked:
 * an outCount larger than the array wrote past its end, and an overflowing
 * product silently turned into an unrelated (or negative) number.
 *
 * swr_convert() consumes every input sample it is given in one call, so there
 * is no point at which a partially-consumed input could be rolled back. The
 * only safe place to refuse is therefore before the call, which is why this
 * runs first: on any failure nothing is converted and nothing is consumed.
 *
 * A NULL input means "drain", exactly as before; the input size is then not
 * required. Returns 0 when the request fits, or a negative AVERROR. */
static int ff_jni_swr_convert_check(int64_t outCount, int64_t outCapacity,
                                    int64_t inCount, int have_in,
                                    int outFmt, int outCh,
                                    int inFmt, int inCh)
{
    int64_t out_bytes;
    int64_t in_bytes;

    if (outCount <= 0 || outCapacity < 0)
        return AVERROR(EINVAL);
    if (outCh <= 0)
        return AVERROR(EINVAL);

    out_bytes = ff_jni_audio_bytes(outCount, outFmt, outCh);
    if (out_bytes < 0)
        return (int)out_bytes;
    /* The array must really hold what the caller asked for. */
    if (out_bytes > outCapacity)
        return AVERROR(ENOMEM);

    /* Input size is arithmetic only: swr_convert() reads outCount samples it
     * was promised, so an input byte count that cannot be formed is a caller
     * bug worth reporting, not something to clamp. */
    in_bytes = have_in ? ff_jni_audio_bytes(inCount, inFmt, inCh) : 0;
    if (in_bytes < 0)
        return (int)in_bytes;
    return 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_swrConvert(JNIEnv *env, jobject thiz, jlong ctx,
                                        jbyteArray out, jint outCount,
                                        jbyteArray in, jint inCount)
{
    struct SwrContext *c = PTR(struct SwrContext *, ctx);
    int64_t out_layout = 0, out_fmt_i = 0;
    int64_t in_layout = 0, in_fmt_i = 0;
    int out_ch, in_ch, check;
    jbyte *out_ptr, *in_ptr = NULL;
    uint8_t *out_bufs[1];
    const uint8_t *in_cb[1] = { NULL };
    int ret;

    if (!c || !out)
        return AVERROR(EINVAL);

    av_opt_get_int(c, "out_channel_layout", 0, &out_layout);
    av_opt_get_int(c, "out_sample_fmt",     0, &out_fmt_i);
    av_opt_get_int(c, "in_channel_layout",  0, &in_layout);
    av_opt_get_int(c, "in_sample_fmt",      0, &in_fmt_i);
    out_ch = ff_jni_mask_channels((uint64_t)out_layout);
    in_ch  = ff_jni_mask_channels((uint64_t)in_layout);

    /* Runs before any byte[] is pinned or any sample is consumed. */
    check = ff_jni_swr_convert_check(outCount,
                                     (*env)->GetArrayLength(env, out),
                                     inCount, in != NULL,
                                     (int)out_fmt_i, out_ch,
                                     (int)in_fmt_i, in_ch);
    if (check < 0)
        return check;

    out_ptr = (*env)->GetByteArrayElements(env, out, NULL);
    if (!out_ptr)
        return AVERROR(ENOMEM);
    out_bufs[0] = (uint8_t *)out_ptr;

    if (in && inCount > 0) {
        in_ptr = (*env)->GetByteArrayElements(env, in, NULL);
        if (!in_ptr) {
            (*env)->ReleaseByteArrayElements(env, out, out_ptr, JNI_ABORT);
            return AVERROR(ENOMEM);
        }
        in_cb[0] = (const uint8_t *)in_ptr;
    }

    ret = swr_convert(c, out_bufs, outCount,
                      in ? in_cb : NULL,
                      in ? inCount : 0);

    (*env)->ReleaseByteArrayElements(env, out, out_ptr, 0);
    if (in_ptr)
        (*env)->ReleaseByteArrayElements(env, in, in_ptr, JNI_ABORT);
    return ret;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_swrFree(JNIEnv *env, jobject thiz, jlong ctx)
{
    struct SwrContext *c = PTR(struct SwrContext *, ctx);
    if (c)
        swr_free(&c);
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_swrGetDelay(JNIEnv *env, jobject thiz, jlong ctx, jlong base)
{
    struct SwrContext *c = PTR(struct SwrContext *, ctx);
    return c ? (jlong)swr_get_delay(c, (int64_t)base) : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_swrGetOutSamples(JNIEnv *env, jobject thiz, jlong ctx, jint inSamples)
{
    struct SwrContext *c = PTR(struct SwrContext *, ctx);
    return c ? (jint)swr_get_out_samples(c, inSamples) : 0;
}

/* ------------------------------------------------------------------ */
/* Bounded PCM resampling session.                                     */
/*                                                                     */
/* swrConvert()/swrConvertFrame() above are a thin passthrough: the    */
/* caller sizes one output block and libswresample is free to buffer   */
/* whatever it could not emit. A Java-side "small block" policy cannot */
/* stop that, because the buffering happens inside swr after the call  */
/* returned, and the JNI layer never sees it.                          */
/*                                                                     */
/* This session owns the SwrContext *and* the limits, so every limit   */
/* is enforced here, before anything is allocated or consumed:         */
/*                                                                     */
/*   - the output block is sized from swr_get_out_samples() after the  */
/*     first frame's real format is known, never guessed from a Java   */
/*     sample count;                                                   */
/*   - a request whose upper bound exceeds maxOutSamplesPerCall is     */
/*     refused with FF_PCM_LIMIT_EXCEEDED and the input is NOT         */
/*     consumed (nothing has entered swr, and swr_convert() has not    */
/*     been called);                                                   */
/*   - the delay swr carries between calls is measured through         */
/*     swr_get_delay() and refused with FF_PCM_LIMIT_EXCEEDED when it  */
/*     exceeds maxDelaySamples, which is what stops a caller from      */
/*     feeding input while never draining it;                          */
/*   - drain and convert share the same checks, so end-of-stream       */
/*     cannot bypass the limits that normal conversion obeys.          */
/*                                                                     */
/* What is NOT bounded: the memory libswresample allocates for its own */
/* filter tables and delay line when the format is first configured.   */
/* swr_init() must build those before a single output sample can be    */
/* produced, so no sample-count check can precede them. The session    */
/* therefore bounds samples in flight and buffers it hands out; it is  */
/* not a hard cap on process memory, and callers must not treat the    */
/* reported numbers as one.                                            */
/* ------------------------------------------------------------------ */

/* Result codes. Negative values are AVERRORs so they compose with the rest
 * of the bridge; the non-negative ones are statuses a caller must branch on
 * rather than treat as "bytes written". */
#define FF_PCM_OUTPUT_READY        0  /* output samples written; input consumed */
#define FF_PCM_INPUT_CONSUMED      1  /* all input taken, no output this call */
#define FF_PCM_NEED_DRAIN          2  /* samples are still buffered in swr */
#define FF_PCM_DRAINED             3  /* nothing left; the tail is complete */
/* The frame's format differs from the configured one and the old context
 * still holds a tail. Nothing was consumed and nothing was reconfigured:
 * the caller must drain the old tail to completion first, then present the
 * same frame again, at which point the format change is committed. This
 * refuses to trade the tail away for the convenience of switching in one
 * call -- dropping it would punch a silent hole in the timeline. */
#define FF_PCM_FORMAT_CHANGE       4

/* Distinct from AVERROR(ENOMEM): "your block is too small for this input"
 * is a caller-visible policy refusal, not an allocation failure, and the
 * caller must not retry it with the same numbers. */
static int ff_pcm_limit_exceeded(void)
{
    return AVERROR(ENOSPC);
}

#define FF_PCM_LIMIT_EXCEEDED ff_pcm_limit_exceeded()

/* Defaults are only a starting point; Java always passes explicit limits. */
typedef struct FFPcmLimits {
    int max_out_samples;   /* per call, per channel */
    int max_in_samples;    /* per call, per channel */
    int max_delay_samples; /* swr internal backlog, in output samples */
} FFPcmLimits;

typedef struct FFPcmSession {
    struct SwrContext *swr;

    FFPcmLimits limits;

    /* Output format is fixed at create time: the player's AudioTrack never
     * changes mid-stream. The layout is kept as an owned AVChannelLayout
     * rather than a raw mask so a custom layout survives the round trip. */
    int             out_fmt;
    int             out_rate;
    AVChannelLayout out_layout;

    /* Input format currently configured in swr. Valid only when configured
     * is set; a format change replaces it transactionally. */
    int             configured;
    int             in_fmt;
    int             in_rate;
    AVChannelLayout in_layout;    /* fully owned deep copy, freed on replace */
} FFPcmSession;

/* Give an UNSPEC frame layout the definite order swr_init() would have
 * derived for it, in place, before the session compares it.
 *
 * Why this is needed here and not just in the caller: swr_init() rewrites an
 * UNSPEC input layout to av_channel_layout_default() (swresample.c, used_ch
 * resolution), so the configured layout is always definite. A decoder that
 * leaves the order unspecified then makes av_channel_layout_compare() report
 * "different" for *every* such frame -- compare() returns 1 whenever exactly
 * one side is UNSPEC. Without this, an alternating UNSPEC/definite decoder
 * would look like a format change on every frame and the session would reply
 * PCM_FORMAT_CHANGE forever: the transaction would never commit because the
 * old tail can never be drained while the caller keeps refetching.
 *
 * Normalising is not a guess: it installs exactly the layout swr would have
 * chosen, so the session and the resampler agree. A frame that already
 * carries a definite layout is never touched.
 *
 * Returns 0 (unchanged or normalised) or a negative AVERROR when the layout
 * cannot be made definite without guessing. */
static int ff_pcm_normalize_layout(AVChannelLayout *layout)
{
    AVChannelLayout next = { 0 };

    if (!layout || !av_channel_layout_check(layout))
        return AVERROR(EINVAL);
    if (layout->order != AV_CHANNEL_ORDER_UNSPEC)
        return 0;

    if (layout->nb_channels <= 0 || layout->nb_channels > 64)
        return AVERROR(EINVAL);
    av_channel_layout_default(&next, layout->nb_channels);
    /* For a count with no standard layout default() leaves UNSPEC, which
     * would not fix anything; refuse rather than loop. */
    if (next.order != AV_CHANNEL_ORDER_NATIVE ||
        next.nb_channels != layout->nb_channels) {
        av_channel_layout_uninit(&next);
        return AVERROR(EINVAL);
    }
    av_channel_layout_uninit(layout);
    *layout = next;
    return 0;
}

/* Layouts are compared by value here, never by pointer: a frame's layout
 * dies with the frame. av_channel_layout_compare() is what decides, so two
 * different masks that describe the same channels stay one format. */
static int ff_pcm_format_matches(const FFPcmSession *s, int fmt, int rate,
                                 const AVChannelLayout *layout)
{
    if (!s->configured)
        return 0;
    if (s->in_fmt != fmt || s->in_rate != rate)
        return 0;
    if (!layout || !av_channel_layout_check(layout))
        return 0;
    if (av_channel_layout_compare(&s->in_layout, layout) != 0)
        return 0;
    return 1;
}

/* Replace the configured input format. The new SwrContext is built and
 * initialised first and only then swapped in, so a failure leaves the
 * session exactly as it was instead of on a half-initialised context. */
static int ff_pcm_configure(FFPcmSession *s, int fmt, int rate,
                            const AVChannelLayout *layout)
{
    struct SwrContext *next = NULL;
    AVChannelLayout copy = { 0 };
    int ret;

    if (!layout || !av_channel_layout_check(layout) || rate <= 0)
        return AVERROR(EINVAL);
    if (!s->out_layout.order || s->out_layout.nb_channels <= 0)
        return AVERROR(EINVAL);

    ret = swr_alloc_set_opts2(&next, &s->out_layout,
                              (enum AVSampleFormat)s->out_fmt, s->out_rate,
                              layout,
                              (enum AVSampleFormat)fmt, rate, 0, NULL);
    if (ret < 0 || !next)
        return ret < 0 ? ret : AVERROR(ENOMEM);

    ret = swr_init(next);
    if (ret < 0) {
        swr_free(&next);
        return ret;
    }

    /* Deep-copy the layout *before* touching the live state: everything that
     * can fail has to fail while the old context is still intact. */
    if (av_channel_layout_copy(&copy, layout) < 0) {
        swr_free(&next);
        return AVERROR(ENOMEM);
    }

    /* Commit: nothing below can fail. */
    swr_free(&s->swr);
    s->swr = next;
    av_channel_layout_uninit(&s->in_layout);
    s->in_layout  = copy;          /* ownership of any map moves in */
    s->in_fmt     = fmt;
    s->in_rate    = rate;
    s->configured = 1;
    return 0;
}

/* Upper bound on what one call may emit for `in_samples` of input, in
 * output samples per channel. Checked arithmetic only: an absurd request is
 * a refusal, never a wrapped small number that then passes the limit test. */
static int64_t ff_pcm_out_bound(FFPcmSession *s, int64_t in_samples)
{
    int64_t bound;

    if (in_samples < 0)
        return AVERROR(EINVAL);
    bound = swr_get_out_samples(s->swr, (int)in_samples);
    if (bound < 0)
        return bound;
    return bound;
}

/* Shared gate for convert and drain.
 *
 * Runs before any byte[] is pinned or any sample is handed to swr. Because
 * swr_convert() takes all of its input in one call, this is the only point
 * at which "refused, input untouched" can still be true. */
static int ff_pcm_check_call(FFPcmSession *s, int64_t in_samples,
                             int64_t out_count, int64_t out_capacity)
{
    int64_t bound, bytes;
    int64_t delay;
    int ret;

    if (!s->swr || !s->configured)
        return AVERROR(EINVAL);
    if (out_count <= 0)
        return AVERROR(EINVAL);
    if (in_samples > s->limits.max_in_samples)
        return FF_PCM_LIMIT_EXCEEDED;

    /* Output block must fit the caller's array before we compute anything
     * else: an under-sized array is a bug, not a budget condition. */
    bytes = ff_jni_audio_bytes(out_count, s->out_fmt,
                               s->out_layout.nb_channels);
    if (bytes < 0)
        return (int)bytes;
    if (bytes > out_capacity)
        return AVERROR(ENOMEM);

    bound = ff_pcm_out_bound(s, in_samples);
    if (bound < 0)
        return (int)bound;
    if (bound > s->limits.max_out_samples)
        return FF_PCM_LIMIT_EXCEEDED;

    /* What swr is already holding. Measured in output samples so it is
     * comparable with the output block and with the limit the caller set. */
    delay = swr_get_delay(s->swr, s->out_rate);
    if (delay < 0)
        return (int)delay;
    if (delay > s->limits.max_delay_samples)
        return FF_PCM_LIMIT_EXCEEDED;

    return 0;
}

/* Convert one frame worth of samples. `in` may be NULL for drain.
 *
 * Returns a non-negative status (FF_PCM_*) with *produced set to the number
 * of output samples written, or a negative AVERROR. On any negative return
 * no input sample has been consumed. */
static int ff_pcm_convert_internal(FFPcmSession *s, const uint8_t *const *in,
                                   int in_samples, uint8_t *const *out,
                                   int out_count, int *produced)
{
    int ret;

    *produced = 0;
    ret = swr_convert(s->swr, out, out_count, in, in_samples);
    if (ret < 0)
        return ret;
    *produced = ret;
    if (ret > 0)
        return FF_PCM_OUTPUT_READY;
    /* Nothing emitted. Distinguish "input accepted, still cooking" from
     * "nothing left", because the caller's cursor advances in the first case
     * and the stream ends in the second. */
    if (in_samples > 0)
        return FF_PCM_INPUT_CONSUMED;
    return swr_get_delay(s->swr, s->out_rate) > 0 ? FF_PCM_NEED_DRAIN
                                                  : FF_PCM_DRAINED;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_pcmSessionCreate(JNIEnv *env, jobject thiz,
                                              jint outFmt, jint outRate,
                                              jlong outLayoutMask,
                                              jint maxOutSamples,
                                              jint maxInSamples,
                                              jint maxDelaySamples)
{
    FFPcmSession *s;
    int ret;

    if (outRate <= 0 || outLayoutMask == 0 ||
        maxOutSamples <= 0 || maxInSamples <= 0 || maxDelaySamples < 0)
        return 0;
    if (av_get_bytes_per_sample((enum AVSampleFormat)outFmt) <= 0)
        return 0;

    s = av_mallocz(sizeof(*s));
    if (!s)
        return 0;

    /* The output layout is owned by the session: copy it, never alias a
     * caller-owned layout that may be freed first. */
    ret = av_channel_layout_from_mask(&s->out_layout, (uint64_t)outLayoutMask);
    if (ret < 0) {
        av_free(s);
        return 0;
    }
    s->out_fmt        = outFmt;
    s->out_rate       = outRate;
    s->limits.max_out_samples   = maxOutSamples;
    s->limits.max_in_samples    = maxInSamples;
    s->limits.max_delay_samples = maxDelaySamples;
    return (jlong)(intptr_t)s;
}

/* Convert one input frame. The frame is read, never retained: a format
 * change re-configures the session from the frame's own values, so the
 * frame must outlive the call but nothing after it. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_pcmSessionConvert(JNIEnv *env, jobject thiz,
                                               jlong handle, jlong frame,
                                               jbyteArray out, jint outCount,
                                               jintArray status)
{
    FFPcmSession *s = PTR(FFPcmSession *, handle);
    AVFrame *f = PTR(AVFrame *, frame);
    int produced = 0;
    int check, ret, in_samples;
    jint zero = 0;
    jbyte *out_ptr;

    if (!s || !f || !out || !status)
        return AVERROR(EINVAL);
    /* Publish "nothing produced" up front so every failure path leaves the
     * caller's status slot defined instead of holding a stale value. */
    if ((*env)->GetArrayLength(env, status) < 1)
        return AVERROR(EINVAL);
    (*env)->SetIntArrayRegion(env, status, 0, 1, &zero);

    in_samples = f->nb_samples;
    if (in_samples < 0 || f->sample_rate <= 0 || f->format < 0 ||
        !av_channel_layout_check(&f->ch_layout))
        return AVERROR(EINVAL);

    /* The per-call input budget is decided *before* any configuration work:
     * configuring allocates libswresample's tables, and a call that is going
     * to be refused must not pay for them. This is the only check that can
     * run before the format is even known to be different. */
    if (in_samples > s->limits.max_in_samples)
        return FF_PCM_LIMIT_EXCEEDED;

    /* Make an UNSPEC layout definite before comparing, so a decoder that
     * does not state an order is not mistaken for a format change on every
     * frame. The frame is documented as exclusively owned by the caller, so
     * modifying it in place is safe and is what makes the comparison stable. */
    check = ff_pcm_normalize_layout(&f->ch_layout);
    if (check < 0)
        return check;

    /* Format snapshot. A change is a transaction, not an in-place edit:
     *
     *   1. if the old context still buffers samples, refuse for now
     *      (FF_PCM_FORMAT_CHANGE, input untouched) so the caller drains the
     *      old tail through the old configuration;
     *   2. once drained, build and validate a new context;
     *   3. convert this same frame exactly once.
     *
     * Reconfiguring immediately would discard the old resampler's tail, and
     * the caller would have no way to detect the gap. */
    if (!ff_pcm_format_matches(s, f->format, f->sample_rate, &f->ch_layout)) {
        if (s->configured && s->swr &&
            swr_get_delay(s->swr, s->out_rate) > 0)
            return FF_PCM_FORMAT_CHANGE;
        check = ff_pcm_configure(s, f->format, f->sample_rate, &f->ch_layout);
        if (check < 0)
            return check;
    }

    /* Remaining limits (output bound, delay, array capacity). The input
     * check is repeated here for the drain-free path but is now a no-op;
     * keeping it in one place would mean configuring before checking. */
    check = ff_pcm_check_call(s, in_samples, outCount,
                              (*env)->GetArrayLength(env, out));
    if (check < 0)
        return check;

    out_ptr = (*env)->GetByteArrayElements(env, out, NULL);
    if (!out_ptr)
        return AVERROR(ENOMEM);

    {
        uint8_t *dst[AV_NUM_DATA_POINTERS] = { (uint8_t *)out_ptr };
        ret = ff_pcm_convert_internal(s, (const uint8_t *const *)f->data,
                                      in_samples, dst, outCount, &produced);
    }

    (*env)->ReleaseByteArrayElements(env, out, out_ptr, 0);
    if (ret < 0)
        return ret;

    (*env)->SetIntArrayRegion(env, status, 0, 1, &produced);
    return ret;
}

/* Emit the buffered tail. Same limits as convert: a drain that could emit
 * more than maxOutSamples is refused rather than silently truncated. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_pcmSessionDrain(JNIEnv *env, jobject thiz,
                                             jlong handle, jbyteArray out,
                                             jint outCount, jintArray status)
{
    FFPcmSession *s = PTR(FFPcmSession *, handle);
    int produced = 0;
    int check, ret;
    jint zero = 0;
    jbyte *out_ptr;

    if (!s || !out || !status)
        return AVERROR(EINVAL);
    if ((*env)->GetArrayLength(env, status) < 1)
        return AVERROR(EINVAL);
    (*env)->SetIntArrayRegion(env, status, 0, 1, &zero);

    /* in_samples == 0: only the delay budget and the output block apply. */
    check = ff_pcm_check_call(s, 0, outCount,
                              (*env)->GetArrayLength(env, out));
    if (check < 0)
        return check;

    out_ptr = (*env)->GetByteArrayElements(env, out, NULL);
    if (!out_ptr)
        return AVERROR(ENOMEM);

    {
        uint8_t *dst[AV_NUM_DATA_POINTERS] = { (uint8_t *)out_ptr };
        ret = ff_pcm_convert_internal(s, NULL, 0, dst, outCount, &produced);
    }

    (*env)->ReleaseByteArrayElements(env, out, out_ptr, 0);
    if (ret < 0)
        return ret;

    (*env)->SetIntArrayRegion(env, status, 0, 1, &produced);
    return ret;
}

/* Drop everything buffered, keeping the configured format. Used for a seek
 * or a discontinuity: the old timeline must not bleed into the new one. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_pcmSessionReset(JNIEnv *env, jobject thiz,
                                             jlong handle)
{
    FFPcmSession *s = PTR(FFPcmSession *, handle);
    struct SwrContext *rebuilt = NULL;
    int ret;

    if (!s)
        return AVERROR(EINVAL);
    if (!s->configured || !s->swr)
        return 0;              /* nothing buffered, nothing to drop */

    /* Building a fresh context is the only way to be sure the delay really
     * reached zero: re-initialising in place leaves the buffer contents to
     * the implementation. The configured format is preserved, so the next
     * convert() is not treated as a format change. */
    ret = swr_alloc_set_opts2(&rebuilt, &s->out_layout,
                              (enum AVSampleFormat)s->out_fmt, s->out_rate,
                              &s->in_layout,
                              (enum AVSampleFormat)s->in_fmt, s->in_rate,
                              0, NULL);
    if (ret < 0 || !rebuilt) {
        swr_free(&rebuilt);
        return ret < 0 ? ret : AVERROR(ENOMEM);
    }
    ret = swr_init(rebuilt);
    if (ret < 0) {
        swr_free(&rebuilt);
        return ret;
    }
    swr_free(&s->swr);
    s->swr = rebuilt;
    return 0;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_pcmSessionFree(JNIEnv *env, jobject thiz, jlong handle)
{
    FFPcmSession *s = PTR(FFPcmSession *, handle);

    if (!s)
        return;
    swr_free(&s->swr);
    av_channel_layout_uninit(&s->in_layout);
    av_channel_layout_uninit(&s->out_layout);
    av_free(s);
}

/* ------------------------------------------------------------------ */
/* Additional libavformat: demux reading / seeking                     */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_readFrame(JNIEnv *env, jobject thiz, jlong ctx, jlong pkt)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    AVPacket *p = PTR(AVPacket *, pkt);
    if (!c || !p)
        return AVERROR(EINVAL);
    return (jint)av_read_frame(c, p);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_seekFrame(JNIEnv *env, jobject thiz, jlong ctx,
                                       jint streamIndex, jlong timestamp, jint flags)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c)
        return AVERROR(EINVAL);
    return (jint)av_seek_frame(c, streamIndex, (int64_t)timestamp, flags);
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_dumpFormat(JNIEnv *env, jobject thiz, jlong ctx,
                                        jint index, jstring url, jint isOutput)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c)
        return;
    const char *u = url ? (*env)->GetStringUTFChars(env, url, NULL) : NULL;
    av_dump_format(c, index, u, isOutput);
    if (u)
        (*env)->ReleaseStringUTFChars(env, url, u);
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_formatFreeContext(JNIEnv *env, jobject thiz, jlong ctx)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (c)
        avformat_free_context(c);
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetTimeBaseNum(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c || index < 0 || index >= (jint)c->nb_streams)
        return 0;
    return (jlong)c->streams[index]->time_base.num;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetTimeBaseDen(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c || index < 0 || index >= (jint)c->nb_streams)
        return 0;
    return (jlong)c->streams[index]->time_base.den;
}

/* ------------------------------------------------------------------ */
/* Additional libavcodec: flush, parameters alloc/from                */
/* ------------------------------------------------------------------ */

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_codecFlushBuffers(JNIEnv *env, jobject thiz, jlong codecCtx)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    if (cc)
        avcodec_flush_buffers(cc);
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_codecparAlloc(JNIEnv *env, jobject thiz)
{
    return (jlong)(intptr_t)avcodec_parameters_alloc();
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_codecparFree(JNIEnv *env, jobject thiz, jlong par)
{
    AVCodecParameters *p = PTR(AVCodecParameters *, par);
    if (p)
        avcodec_parameters_free(&p);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_codecparFromContext(JNIEnv *env, jobject thiz, jlong par, jlong codecCtx)
{
    AVCodecParameters *p = PTR(AVCodecParameters *, par);
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    if (!p || !cc)
        return AVERROR(EINVAL);
    return (jint)avcodec_parameters_from_context(p, cc);
}

/* ------------------------------------------------------------------ */
/* Additional libavformat: muxing / output                              */
/* ------------------------------------------------------------------ */

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_formatAllocOutputContext2(JNIEnv *env, jobject thiz,
                                                      jstring formatName, jstring filename)
{
    AVFormatContext *ctx = NULL;
    const char *fmt = formatName ? (*env)->GetStringUTFChars(env, formatName, NULL) : NULL;
    const char *fn  = filename   ? (*env)->GetStringUTFChars(env, filename, NULL)   : NULL;
    int ret = avformat_alloc_output_context2(&ctx, NULL, fmt, fn);
    if (formatName) (*env)->ReleaseStringUTFChars(env, formatName, fmt);
    if (filename)   (*env)->ReleaseStringUTFChars(env, filename, fn);
    if (ret < 0 || !ctx)
        return 0;
    return (jlong)(intptr_t)ctx;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_formatNewStream(JNIEnv *env, jobject thiz, jlong ctx, jlong codec)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    AVCodec *co = PTR(AVCodec *, codec);
    if (!c)
        return AVERROR(EINVAL);
    AVStream *st = avformat_new_stream(c, co);
    if (!st)
        return AVERROR(ENOMEM);
    return (jint)(st->index);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_ioOpen(JNIEnv *env, jobject thiz, jlong ctx, jstring url, jint flags)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c || !url)
        return AVERROR(EINVAL);
    const char *u = (*env)->GetStringUTFChars(env, url, NULL);
    int ret = avio_open(&c->pb, u, flags);
    (*env)->ReleaseStringUTFChars(env, url, u);
    return ret;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_ioClose(JNIEnv *env, jobject thiz, jlong ctx)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c || !c->pb)
        return AVERROR(EINVAL);
    return (jint)avio_closep(&c->pb);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_formatWriteHeader(JNIEnv *env, jobject thiz, jlong ctx, jlong optionsBox)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    AVDictionary **opts = optionsBox ? PTR(AVDictionary **, optionsBox) : NULL;
    if (!c)
        return AVERROR(EINVAL);
    return (jint)avformat_write_header(c, opts);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_writeFrame(JNIEnv *env, jobject thiz, jlong ctx, jlong pkt)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    AVPacket *p = PTR(AVPacket *, pkt);
    if (!c)
        return AVERROR(EINVAL);
    return (jint)av_write_frame(c, p);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_interleavedWriteFrame(JNIEnv *env, jobject thiz, jlong ctx, jlong pkt)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    AVPacket *p = PTR(AVPacket *, pkt);
    if (!c)
        return AVERROR(EINVAL);
    return (jint)av_interleaved_write_frame(c, p);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_writeTrailer(JNIEnv *env, jobject thiz, jlong ctx)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c)
        return AVERROR(EINVAL);
    return (jint)av_write_trailer(c);
}

/* ------------------------------------------------------------------ */
/* Additional AVPacket helpers                                          */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_newPacket(JNIEnv *env, jobject thiz, jlong pkt, jint size)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    if (!p)
        return AVERROR(EINVAL);
    return (jint)av_new_packet(p, size);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_packetMakeWritable(JNIEnv *env, jobject thiz, jlong pkt)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    if (!p)
        return AVERROR(EINVAL);
    return (jint)av_packet_make_writable(p);
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_packetRescaleTs(JNIEnv *env, jobject thiz, jlong pkt,
                                             jint srcNum, jint srcDen, jint dstNum, jint dstDen)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    if (!p)
        return;
    AVRational src = { srcNum, srcDen };
    AVRational dst = { dstNum, dstDen };
    av_packet_rescale_ts(p, src, dst);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_packetCopyFrom(JNIEnv *env, jobject thiz, jlong pkt, jbyteArray in, jint off, jint len)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    if (!p || !in || len <= 0)
        return AVERROR(EINVAL);
    if (av_new_packet(p, len) < 0)
        return AVERROR(ENOMEM);
    (*env)->GetByteArrayRegion(env, in, off, len, (jbyte *)p->data);
    return len;
}

/* ------------------------------------------------------------------ */
/* Additional AVFrame helpers                                          */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetBuffer(JNIEnv *env, jobject thiz, jlong frame, jint align)
{
    AVFrame *f = PTR(AVFrame *, frame);
    if (!f)
        return AVERROR(EINVAL);
    return (jint)av_frame_get_buffer(f, align);
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_frameSetVideoFormat(JNIEnv *env, jobject thiz, jlong frame,
                                                  jint pixFmt, jint width, jint height)
{
    AVFrame *f = PTR(AVFrame *, frame);
    if (!f)
        return;
    f->format = pixFmt;
    f->width  = width;
    f->height = height;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_frameSetAudioFormat(JNIEnv *env, jobject thiz, jlong frame,
                                                  jint sampleFmt, jint sampleRate, jint channels)
{
    AVFrame *f = PTR(AVFrame *, frame);
    if (!f)
        return;
    f->format = sampleFmt;
    f->sample_rate = sampleRate;
    av_channel_layout_default(&f->ch_layout, channels);
}

/* ------------------------------------------------------------------ */
/* Additional helpers: dict copy, math, format names                   */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_dictCopy(JNIEnv *env, jobject thiz, jlong dstBox, jlong srcBox, jint flags)
{
    AVDictionary **dst = PTR(AVDictionary **, dstBox);
    AVDictionary **src = srcBox ? PTR(AVDictionary **, srcBox) : NULL;
    if (!dst)
        return AVERROR(EINVAL);
    return (jint)av_dict_copy(dst, src ? *src : NULL, flags);
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_rescaleQ(JNIEnv *env, jobject thiz, jlong a, jint bqNum, jint bqDen, jint cqNum, jint cqDen)
{
    AVRational bq = { bqNum, bqDen };
    AVRational cq = { cqNum, cqDen };
    return (jlong)av_rescale_q((int64_t)a, bq, cq);
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_getPixFmtName(JNIEnv *env, jobject thiz, jint pixFmt)
{
    const char *n = av_get_pix_fmt_name((enum AVPixelFormat)pixFmt);
    return n ? (*env)->NewStringUTF(env, n) : NULL;
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_getSampleFmtName(JNIEnv *env, jobject thiz, jint sampleFmt)
{
    const char *n = av_get_sample_fmt_name((enum AVSampleFormat)sampleFmt);
    return n ? (*env)->NewStringUTF(env, n) : NULL;
}

/* ------------------------------------------------------------------ */
/* Codec context setters/getters (encoding configuration)              */
/* ------------------------------------------------------------------ */

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_setContextBitRate(JNIEnv *env, jobject thiz, jlong codecCtx, jlong bitRate)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    if (cc) cc->bit_rate = (int64_t)bitRate;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_setContextGopSize(JNIEnv *env, jobject thiz, jlong codecCtx, jint gop)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    if (cc) cc->gop_size = gop;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_setContextMaxBFrames(JNIEnv *env, jobject thiz, jlong codecCtx, jint maxBFrames)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    if (cc) cc->max_b_frames = maxBFrames;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_setContextGlobalQuality(JNIEnv *env, jobject thiz, jlong codecCtx, jint quality)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    if (cc) cc->global_quality = quality;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_setContextQmin(JNIEnv *env, jobject thiz, jlong codecCtx, jint qmin)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    if (cc) cc->qmin = qmin;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_setContextQmax(JNIEnv *env, jobject thiz, jlong codecCtx, jint qmax)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    if (cc) cc->qmax = qmax;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_setContextThreadCount(JNIEnv *env, jobject thiz, jlong codecCtx, jint count)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    if (cc) cc->thread_count = count;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_setContextPixFmt(JNIEnv *env, jobject thiz, jlong codecCtx, jint pixFmt)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    if (cc) cc->pix_fmt = (enum AVPixelFormat)pixFmt;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_setContextWidth(JNIEnv *env, jobject thiz, jlong codecCtx, jint width)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    if (cc) cc->width = width;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_setContextHeight(JNIEnv *env, jobject thiz, jlong codecCtx, jint height)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    if (cc) cc->height = height;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_setContextTimeBase(JNIEnv *env, jobject thiz, jlong codecCtx, jint num, jint den)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    if (cc) { cc->time_base.num = num; cc->time_base.den = den; }
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_setContextPktTimebase(JNIEnv *env, jobject thiz, jlong codecCtx, jint num, jint den)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    if (cc) { cc->pkt_timebase.num = num; cc->pkt_timebase.den = den; }
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_setContextFramerate(JNIEnv *env, jobject thiz, jlong codecCtx, jint num, jint den)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    if (cc) { cc->framerate.num = num; cc->framerate.den = den; }
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_setContextSampleRate(JNIEnv *env, jobject thiz, jlong codecCtx, jint rate)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    if (cc) cc->sample_rate = rate;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_setContextSampleFmt(JNIEnv *env, jobject thiz, jlong codecCtx, jint sampleFmt)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    if (cc) cc->sample_fmt = (enum AVSampleFormat)sampleFmt;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_setContextChannels(JNIEnv *env, jobject thiz, jlong codecCtx, jint channels)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    if (cc) av_channel_layout_default(&cc->ch_layout, channels);
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_getContextBitRate(JNIEnv *env, jobject thiz, jlong codecCtx)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    return cc ? (jlong)cc->bit_rate : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_getContextGopSize(JNIEnv *env, jobject thiz, jlong codecCtx)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    return cc ? (jint)cc->gop_size : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_getContextSampleRate(JNIEnv *env, jobject thiz, jlong codecCtx)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    return cc ? (jint)cc->sample_rate : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_getContextSampleFmt(JNIEnv *env, jobject thiz, jlong codecCtx)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    return cc ? (jint)cc->sample_fmt : -1;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_getContextChannels(JNIEnv *env, jobject thiz, jlong codecCtx)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    return cc ? (jint)cc->ch_layout.nb_channels : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_getContextTimeBaseNum(JNIEnv *env, jobject thiz, jlong codecCtx)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    return cc ? (jint)cc->time_base.num : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_getContextTimeBaseDen(JNIEnv *env, jobject thiz, jlong codecCtx)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    return cc ? (jint)cc->time_base.den : 0;
}

/* ------------------------------------------------------------------ */
/* Generic option API (av_opt_*): set codec/format options             */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_optSetString(JNIEnv *env, jobject thiz, jlong obj, jstring name, jstring val)
{
    void *o = PTR(void *, obj);
    if (!o || !name)
        return AVERROR(EINVAL);
    const char *n = (*env)->GetStringUTFChars(env, name, NULL);
    const char *v = val ? (*env)->GetStringUTFChars(env, val, NULL) : NULL;
    int ret = av_opt_set(o, n, v, 0);
    (*env)->ReleaseStringUTFChars(env, name, n);
    if (val)
        (*env)->ReleaseStringUTFChars(env, val, v);
    return ret;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_optSetInt(JNIEnv *env, jobject thiz, jlong obj, jstring name, jlong val)
{
    void *o = PTR(void *, obj);
    if (!o || !name)
        return AVERROR(EINVAL);
    const char *n = (*env)->GetStringUTFChars(env, name, NULL);
    int ret = av_opt_set_int(o, n, (int64_t)val, 0);
    (*env)->ReleaseStringUTFChars(env, name, n);
    return ret;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_optSetDouble(JNIEnv *env, jobject thiz, jlong obj, jstring name, jdouble val)
{
    void *o = PTR(void *, obj);
    if (!o || !name)
        return AVERROR(EINVAL);
    const char *n = (*env)->GetStringUTFChars(env, name, NULL);
    int ret = av_opt_set_double(o, n, (double)val, 0);
    (*env)->ReleaseStringUTFChars(env, name, n);
    return ret;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_optGetInt(JNIEnv *env, jobject thiz, jlong obj, jstring name)
{
    void *o = PTR(void *, obj);
    if (!o || !name)
        return AVERROR(EINVAL);
    const char *n = (*env)->GetStringUTFChars(env, name, NULL);
    int64_t v = 0;
    int ret = av_opt_get_int(o, n, 0, &v);
    (*env)->ReleaseStringUTFChars(env, name, n);
    return (ret < 0) ? (jlong)ret : (jlong)v;
}

/* ------------------------------------------------------------------ */
/* Logging                                                             */
/* ------------------------------------------------------------------ */

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_logSetLevel(JNIEnv *env, jobject thiz, jint level)
{
    av_log_set_level(level);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_logGetLevel(JNIEnv *env, jobject thiz)
{
    return (jint)av_log_get_level();
}

/* ------------------------------------------------------------------ */
/* Log callback forwarding to Android Logcat */
/* ------------------------------------------------------------------ */

static void android_log_callback(void *ptr, int level, const char *fmt, va_list vl)
{
    int android_level;

    /* av_vlog() does not filter by level; that check lives inside
     * av_log_default_callback, which this callback replaces. Without it
     * av_log_set_level()/logSetLevel() has no effect and TRACE-level decoder
     * chatter floods logcat. The high byte may carry a color tint, so mask it
     * off before comparing (see libavutil/log.c). */
    if (level >= 0)
        level &= 0xff;
    if (level > av_log_get_level())
        return;

    switch (level) {
        case AV_LOG_PANIC:
        case AV_LOG_FATAL:
            android_level = ANDROID_LOG_FATAL;
            break;
        case AV_LOG_ERROR:
            android_level = ANDROID_LOG_ERROR;
            break;
        case AV_LOG_WARNING:
            android_level = ANDROID_LOG_WARN;
            break;
        case AV_LOG_INFO:
            android_level = ANDROID_LOG_INFO;
            break;
        case AV_LOG_VERBOSE:
            android_level = ANDROID_LOG_VERBOSE;
            break;
        case AV_LOG_DEBUG:
        case AV_LOG_TRACE:
        default:
            android_level = ANDROID_LOG_DEBUG;
            break;
    }
    char line[1024];
    vsnprintf(line, sizeof(line), fmt, vl);
    __android_log_write(android_level, "FFmpeg", line);
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_logSetCallback(JNIEnv *env, jobject thiz)
{
    av_log_set_callback(android_log_callback);
}

/* ------------------------------------------------------------------ */
/* Hardware frames context attachment */
/* ------------------------------------------------------------------ */

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_setContextHwFramesCtx(JNIEnv *env, jobject thiz, jlong codecCtx, jlong hwFramesCtx)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    AVBufferRef *ref = (AVBufferRef *)(intptr_t)hwFramesCtx;
    if (cc && ref) {
        /* Drop any previously-attached ref first to avoid leaking it. */
        av_buffer_unref(&cc->hw_frames_ctx);
        AVBufferRef *copy = av_buffer_ref(ref);
        if (copy)
            cc->hw_frames_ctx = copy;
    }
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_setContextHwDeviceCtx(JNIEnv *env, jobject thiz,
                                                    jlong codecCtx, jlong hwDeviceCtx)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    AVBufferRef *ref = (AVBufferRef *)(intptr_t)hwDeviceCtx;
    if (!cc || !ref)
        return AVERROR(EINVAL);
    /* Take the new reference first: on allocation failure the context must
     * keep whatever device it already had instead of ending up detached. */
    AVBufferRef *copy = av_buffer_ref(ref);
    if (!copy)
        return AVERROR(ENOMEM);
    av_buffer_unref(&cc->hw_device_ctx);
    cc->hw_device_ctx = copy;
    return 0;
}

/* ------------------------------------------------------------------ */
/* av_hwframe_map wrapper */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_hwframeMap(JNIEnv *env, jobject thiz, jlong dstFrame, jlong srcFrame, jint flags)
{
    AVFrame *dst = PTR(AVFrame *, dstFrame);
    AVFrame *src = PTR(AVFrame *, srcFrame);
    if (!dst || !src)
        return AVERROR(EINVAL);
    return av_hwframe_map(dst, src, flags);
}

/* ------------------------------------------------------------------ */
/* Channel layout parsing from string */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_channelLayoutFromString(JNIEnv *env, jobject thiz, jstring layoutStr)
{
    if (!layoutStr)
        return AVERROR(EINVAL);
    const char *cstr = (*env)->GetStringUTFChars(env, layoutStr, NULL);
    AVChannelLayout layout;
    int ret = av_channel_layout_from_string(&layout, cstr);
    (*env)->ReleaseStringUTFChars(env, layoutStr, cstr);
    if (ret < 0)
        return ret;
    return (jint)layout.u.mask;
}

/* ------------------------------------------------------------------ */
/* avfilter_link wrapper */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_filterLink(JNIEnv *env, jobject thiz, jlong srcCtx, jlong dstCtx, jint srcPadIdx, jint dstPadIdx)
{
    AVFilterContext *src = PTR(AVFilterContext *, srcCtx);
    AVFilterContext *dst = PTR(AVFilterContext *, dstCtx);
    if (!src || !dst)
        return AVERROR(EINVAL);
    return avfilter_link(src, srcPadIdx, dst, dstPadIdx);
}

/* ------------------------------------------------------------------ */
/* avfilter_graph_get_filter wrapper */
/* ------------------------------------------------------------------ */

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_filterGraphGetFilter(JNIEnv *env, jobject thiz, jlong graph, jstring name)
{
    if (!graph || !name)
        return 0;
    const char *cname = (*env)->GetStringUTFChars(env, name, NULL);
    AVFilterContext *f = avfilter_graph_get_filter((AVFilterGraph *)(intptr_t)graph, cname);
    (*env)->ReleaseStringUTFChars(env, name, cname);
    return (jlong)(intptr_t)f;
}

/* ------------------------------------------------------------------ */
/* Channel layout description wrappers */
/* ------------------------------------------------------------------ */

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_channelLayoutDescribe(JNIEnv *env, jobject thiz, jlong mask)
{
    AVChannelLayout layout;
    if (av_channel_layout_from_mask(&layout, (uint64_t)mask) < 0) {
        char empty[1] = {0};
        return (*env)->NewStringUTF(env, empty);
    }
    char buf[128];
    av_channel_layout_describe(&layout, buf, sizeof(buf));
    av_channel_layout_uninit(&layout);
    return (*env)->NewStringUTF(env, buf);
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_channelLayoutToString(JNIEnv *env, jobject thiz, jlong mask)
{
    AVChannelLayout layout;
    if (av_channel_layout_from_mask(&layout, (uint64_t)mask) < 0) {
        char empty[1] = {0};
        return (*env)->NewStringUTF(env, empty);
    }
    char buf[128];
    av_channel_layout_describe(&layout, buf, sizeof(buf));
    av_channel_layout_uninit(&layout);
    return (*env)->NewStringUTF(env, buf);
}

/* ------------------------------------------------------------------ */
/* Mathematics                                                         */
/* ------------------------------------------------------------------ */

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_rescale(JNIEnv *env, jobject thiz, jlong a, jlong b, jlong c)
{
    return (jlong)av_rescale((int64_t)a, (int64_t)b, (int64_t)c);
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_rescaleRnd(JNIEnv *env, jobject thiz, jlong a, jlong b, jlong c, jint rnd)
{
    return (jlong)av_rescale_rnd((int64_t)a, (int64_t)b, (int64_t)c, (enum AVRounding)rnd);
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_rescaleQRnd(JNIEnv *env, jobject thiz, jlong a,
                                         jint bqNum, jint bqDen, jint cqNum, jint cqDen, jint rnd)
{
    AVRational bq = { bqNum, bqDen };
    AVRational cq = { cqNum, cqDen };
    return (jlong)av_rescale_q_rnd((int64_t)a, bq, cq, (enum AVRounding)rnd);
}

/* ------------------------------------------------------------------ */
/* Format name lookups                                                 */
/* ------------------------------------------------------------------ */

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_getMediaTypeString(JNIEnv *env, jobject thiz, jint mediaType)
{
    const char *n = av_get_media_type_string((enum AVMediaType)mediaType);
    return n ? (*env)->NewStringUTF(env, n) : NULL;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_getPixFmt(JNIEnv *env, jobject thiz, jstring name)
{
    if (!name)
        return AV_PIX_FMT_NONE;
    const char *n = (*env)->GetStringUTFChars(env, name, NULL);
    int ret = (int)av_get_pix_fmt(n);
    (*env)->ReleaseStringUTFChars(env, name, n);
    return ret;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_getSampleFmt(JNIEnv *env, jobject thiz, jstring name)
{
    if (!name)
        return (jint)AV_SAMPLE_FMT_NONE;
    const char *n = (*env)->GetStringUTFChars(env, name, NULL);
    int ret = (int)av_get_sample_fmt(n);
    (*env)->ReleaseStringUTFChars(env, name, n);
    return ret;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_getCodecIdByName(JNIEnv *env, jobject thiz, jstring name)
{
    if (!name)
        return AV_CODEC_ID_NONE;
    const char *n = (*env)->GetStringUTFChars(env, name, NULL);
    const AVCodecDescriptor *d = avcodec_descriptor_get_by_name(n);
    (*env)->ReleaseStringUTFChars(env, name, n);
    return d ? (jint)d->id : AV_CODEC_ID_NONE;
}

/* ------------------------------------------------------------------ */
/* Additional AVFrame: make writable                                   */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameMakeWritable(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    if (!f)
        return AVERROR(EINVAL);
    return (jint)av_frame_make_writable(f);
}

/* ------------------------------------------------------------------ */
/* Additional libavformat: flexible seek / flush / input format         */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_formatSeekFile(JNIEnv *env, jobject thiz, jlong ctx,
                                            jint streamIndex, jlong minTs, jlong ts, jlong maxTs, jint flags)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c)
        return AVERROR(EINVAL);
    return (jint)avformat_seek_file(c, streamIndex, (int64_t)minTs, (int64_t)ts, (int64_t)maxTs, flags);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_formatFlush(JNIEnv *env, jobject thiz, jlong ctx)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c)
        return AVERROR(EINVAL);
    return (jint)avformat_flush(c);
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_getFormatTimeBaseQ(JNIEnv *env, jobject thiz)
{
    AVRational tb = av_get_time_base_q();
    char buf[64];
    snprintf(buf, sizeof(buf), "%d/%d", tb.num, tb.den);
    return (*env)->NewStringUTF(env, buf);
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_formatGetInputFormatName(JNIEnv *env, jobject thiz, jlong ctx)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c || !c->iformat || !c->iformat->name)
        return NULL;
    return (*env)->NewStringUTF(env, c->iformat->name);
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_formatGetOutputFormatName(JNIEnv *env, jobject thiz, jlong ctx)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c || !c->oformat || !c->oformat->name)
        return NULL;
    return (*env)->NewStringUTF(env, c->oformat->name);
}

/* ------------------------------------------------------------------ */
/* Additional stream getters: frame rates                               */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetAvgFrameRateNum(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c || index < 0 || index >= (jint)c->nb_streams)
        return 0;
    return (jint)c->streams[index]->avg_frame_rate.num;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetAvgFrameRateDen(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c || index < 0 || index >= (jint)c->nb_streams)
        return 0;
    return (jint)c->streams[index]->avg_frame_rate.den;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetRFrameRateNum(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c || index < 0 || index >= (jint)c->nb_streams)
        return 0;
    return (jint)c->streams[index]->r_frame_rate.num;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetRFrameRateDen(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c || index < 0 || index >= (jint)c->nb_streams)
        return 0;
    return (jint)c->streams[index]->r_frame_rate.den;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetParBitRate(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVCodecParameters *p = stream_par(PTR(AVFormatContext *, ctx), index);
    return p ? (jint)p->bit_rate : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetParNbFrames(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c || index < 0 || index >= (jint)c->nb_streams)
        return 0;
    return (jint)c->streams[index]->nb_frames;
}

/* ------------------------------------------------------------------ */
/* Bitstream filter API (AnnexB <-> AVCC etc.)                         */
/* ------------------------------------------------------------------ */

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_bsfAllocByName(JNIEnv *env, jobject thiz, jstring name)
{
    if (!name)
        return 0;
    const char *n = (*env)->GetStringUTFChars(env, name, NULL);
    const AVBitStreamFilter *f = av_bsf_get_by_name(n);
    (*env)->ReleaseStringUTFChars(env, name, n);
    if (!f)
        return 0;
    AVBSFContext *ctx = NULL;
    if (av_bsf_alloc(f, &ctx) < 0 || !ctx)
        return 0;
    return (jlong)(intptr_t)ctx;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_bsfCopyInputPar(JNIEnv *env, jobject thiz, jlong bsfCtx, jlong fmtCtx, jint index)
{
    AVBSFContext *bc = PTR(AVBSFContext *, bsfCtx);
    AVFormatContext *fc = PTR(AVFormatContext *, fmtCtx);
    AVCodecParameters *p = stream_par(fc, index);
    if (!bc || !p)
        return AVERROR(EINVAL);
    return (jint)avcodec_parameters_copy(bc->par_in, p);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_bsfInit(JNIEnv *env, jobject thiz, jlong bsfCtx)
{
    AVBSFContext *bc = PTR(AVBSFContext *, bsfCtx);
    return bc ? (jint)av_bsf_init(bc) : AVERROR(EINVAL);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_bsfSendPacket(JNIEnv *env, jobject thiz, jlong bsfCtx, jlong pkt)
{
    AVBSFContext *bc = PTR(AVBSFContext *, bsfCtx);
    AVPacket *p = pkt ? PTR(AVPacket *, pkt) : NULL;
    if (!bc)
        return AVERROR(EINVAL);
    return (jint)av_bsf_send_packet(bc, p);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_bsfReceivePacket(JNIEnv *env, jobject thiz, jlong bsfCtx, jlong pkt)
{
    AVBSFContext *bc = PTR(AVBSFContext *, bsfCtx);
    AVPacket *p = PTR(AVPacket *, pkt);
    if (!bc || !p)
        return AVERROR(EINVAL);
    return (jint)av_bsf_receive_packet(bc, p);
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_bsfFree(JNIEnv *env, jobject thiz, jlong bsfCtx)
{
    AVBSFContext *bc = PTR(AVBSFContext *, bsfCtx);
    if (bc)
        av_bsf_free(&bc);
}

/* ------------------------------------------------------------------ */
/* libavfilter graph (transform pipelines)                             */
/* ------------------------------------------------------------------ */

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_filterGraphAlloc(JNIEnv *env, jobject thiz)
{
    return (jlong)(intptr_t)avfilter_graph_alloc();
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_filterGraphFree(JNIEnv *env, jobject thiz, jlong graph)
{
    AVFilterGraph *g = PTR(AVFilterGraph *, graph);
    if (g)
        avfilter_graph_free(&g);
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_filterGetByName(JNIEnv *env, jobject thiz, jstring name)
{
    if (!name)
        return 0;
    const char *n = (*env)->GetStringUTFChars(env, name, NULL);
    const AVFilter *f = avfilter_get_by_name(n);
    (*env)->ReleaseStringUTFChars(env, name, n);
    return (jlong)(intptr_t)f;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_filterGraphCreateFilter(JNIEnv *env, jobject thiz,
                                                     jlong graph, jlong filter, jstring name, jstring args)
{
    AVFilterGraph *g = PTR(AVFilterGraph *, graph);
    const AVFilter *f = PTR(const AVFilter *, filter);
    if (!g || !f)
        return 0;
    const char *n = name ? (*env)->GetStringUTFChars(env, name, NULL) : NULL;
    const char *a = args ? (*env)->GetStringUTFChars(env, args, NULL) : NULL;
    AVFilterContext *ctx = NULL;
    int ret = avfilter_graph_create_filter(&ctx, f, n, a, NULL, g);
    if (name) (*env)->ReleaseStringUTFChars(env, name, n);
    if (args) (*env)->ReleaseStringUTFChars(env, args, a);
    if (ret < 0 || !ctx)
        return 0;
    return (jlong)(intptr_t)ctx;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_filterGraphParsePtr(JNIEnv *env, jobject thiz,
                                                 jlong graph, jstring filters, jlong srcCtx, jlong sinkCtx)
{
    AVFilterGraph *g = PTR(AVFilterGraph *, graph);
    if (!g || !filters)
        return AVERROR(EINVAL);
    const char *f = (*env)->GetStringUTFChars(env, filters, NULL);
    /* outputs list connects the [in] label to the source filter context;
     * inputs list connects the [out] label to the sink filter context. */
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    if (srcCtx) {
        outputs->name       = av_strdup("in");
        outputs->filter_ctx = PTR(AVFilterContext *, srcCtx);
        outputs->pad_idx    = 0;
        outputs->next       = NULL;
    }
    if (sinkCtx) {
        inputs->name       = av_strdup("out");
        inputs->filter_ctx = PTR(AVFilterContext *, sinkCtx);
        inputs->pad_idx    = 0;
        inputs->next       = NULL;
    }
    int ret = avfilter_graph_parse_ptr(g, f, &inputs, &outputs, NULL);
    (*env)->ReleaseStringUTFChars(env, filters, f);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    return ret;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_filterGraphConfig(JNIEnv *env, jobject thiz, jlong graph)
{
    AVFilterGraph *g = PTR(AVFilterGraph *, graph);
    return g ? (jint)avfilter_graph_config(g, NULL) : AVERROR(EINVAL);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_bufferSrcAddFrame(JNIEnv *env, jobject thiz, jlong srcCtx, jlong frame)
{
    AVFilterContext *c = PTR(AVFilterContext *, srcCtx);
    AVFrame *f = frame ? PTR(AVFrame *, frame) : NULL;
    if (!c)
        return AVERROR(EINVAL);
    return (jint)av_buffersrc_add_frame(c, f);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_bufferSinkGetFrame(JNIEnv *env, jobject thiz, jlong sinkCtx, jlong frame)
{
    AVFilterContext *c = PTR(AVFilterContext *, sinkCtx);
    AVFrame *f = PTR(AVFrame *, frame);
    if (!c || !f)
        return AVERROR(EINVAL);
    return (jint)av_buffersink_get_frame(c, f);
}

/* ------------------------------------------------------------------ */
/* Buffer-size helpers                                                  */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_imageGetBufferSize(JNIEnv *env, jobject thiz,
                                                jint pixFmt, jint width, jint height, jint align)
{
    return (jint)av_image_get_buffer_size((enum AVPixelFormat)pixFmt, width, height, align);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_samplesGetBufferSize(JNIEnv *env, jobject thiz,
                                                  jint nbChannels, jint nbSamples, jint sampleFmt, jint align)
{
    return (jint)av_samples_get_buffer_size(NULL, nbChannels, nbSamples,
                                            (enum AVSampleFormat)sampleFmt, align);
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_bytesPerSample(JNIEnv *env, jobject thiz, jint sampleFmt)
{
    return (jint)av_get_bytes_per_sample((enum AVSampleFormat)sampleFmt);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_bitsPerPixel(JNIEnv *env, jobject thiz, jint pixFmt)
{
    return (jint)av_get_bits_per_pixel(av_pix_fmt_desc_get((enum AVPixelFormat)pixFmt));
}

/* ================================================================== */
/* Round 3: additional public API gap coverage                         */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* libavformat: network, manual alloc, format probing                  */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_formatNetworkInit(JNIEnv *env, jobject thiz)
{
    return (jint)avformat_network_init();
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_formatNetworkDeinit(JNIEnv *env, jobject thiz)
{
    return (jint)avformat_network_deinit();
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_formatAllocContext(JNIEnv *env, jobject thiz)
{
    return (jlong)(intptr_t)avformat_alloc_context();
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_findInputFormat(JNIEnv *env, jobject thiz, jstring name)
{
    if (!name)
        return 0;
    const char *n = (*env)->GetStringUTFChars(env, name, NULL);
    const AVInputFormat *f = av_find_input_format(n);
    (*env)->ReleaseStringUTFChars(env, name, n);
    return (jlong)(intptr_t)f;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_guessFormat(JNIEnv *env, jobject thiz,
                                         jstring shortName, jstring filename, jstring mime)
{
    const char *s = shortName ? (*env)->GetStringUTFChars(env, shortName, NULL) : NULL;
    const char *f = filename  ? (*env)->GetStringUTFChars(env, filename,  NULL) : NULL;
    const char *m = mime      ? (*env)->GetStringUTFChars(env, mime,       NULL) : NULL;
    const AVOutputFormat *of = av_guess_format(s, f, m);
    if (shortName) (*env)->ReleaseStringUTFChars(env, shortName, s);
    if (filename)  (*env)->ReleaseStringUTFChars(env, filename, f);
    if (mime)      (*env)->ReleaseStringUTFChars(env, mime, m);
    return (jlong)(intptr_t)of;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_formatQueryCodec(JNIEnv *env, jobject thiz,
                                              jlong ctx, jint codecId, jint stdCompliance)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c || !c->oformat)
        return AVERROR(EINVAL);
    return (jint)avformat_query_codec(c->oformat, (enum AVCodecID)codecId, stdCompliance);
}

/* ------------------------------------------------------------------ */
/* libavcodec: lookup by name, iterate, name-by-pointer, audio dur    */
/* ------------------------------------------------------------------ */

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_findDecoderByName(JNIEnv *env, jobject thiz, jstring name)
{
    if (!name)
        return 0;
    const char *n = (*env)->GetStringUTFChars(env, name, NULL);
    const AVCodec *c = avcodec_find_decoder_by_name(n);
    (*env)->ReleaseStringUTFChars(env, name, n);
    return (jlong)(intptr_t)c;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_findEncoderByName(JNIEnv *env, jobject thiz, jstring name)
{
    if (!name)
        return 0;
    const char *n = (*env)->GetStringUTFChars(env, name, NULL);
    const AVCodec *c = avcodec_find_encoder_by_name(n);
    (*env)->ReleaseStringUTFChars(env, name, n);
    return (jlong)(intptr_t)c;
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_codecGetLongName(JNIEnv *env, jobject thiz, jlong codec)
{
    const AVCodec *c = PTR(const AVCodec *, codec);
    return (c && c->long_name) ? (*env)->NewStringUTF(env, c->long_name) : NULL;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_codecGetType(JNIEnv *env, jobject thiz, jlong codec)
{
    const AVCodec *c = PTR(const AVCodec *, codec);
    return c ? (jint)c->type : (jint)AVMEDIA_TYPE_UNKNOWN;
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_codecGetPtrName(JNIEnv *env, jobject thiz, jlong codec)
{
    const AVCodec *c = PTR(const AVCodec *, codec);
    const char *n = c ? avcodec_get_name(c->id) : NULL;
    return n ? (*env)->NewStringUTF(env, n) : NULL;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_getAudioFrameDuration(JNIEnv *env, jobject thiz,
                                                   jlong codecCtx, jint frameBytes)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    return cc ? (jint)av_get_audio_frame_duration(cc, frameBytes) : AVERROR(EINVAL);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_getAudioFrameDuration2(JNIEnv *env, jobject thiz,
                                                     jlong fmtCtx, jint index, jint frameBytes)
{
    AVCodecParameters *p = stream_par(PTR(AVFormatContext *, fmtCtx), index);
    return p ? (jint)av_get_audio_frame_duration2(p, frameBytes) : AVERROR(EINVAL);
}

/* ------------------------------------------------------------------ */
/* Hardware acceleration (basic apparatus)                              */
/* ------------------------------------------------------------------ */

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_hwdeviceCreate(JNIEnv *env, jobject thiz, jint hwType, jstring device, jint opts)
{
    const char *d = device ? (*env)->GetStringUTFChars(env, device, NULL) : NULL;
    AVBufferRef *ref = NULL;
    int ret = av_hwdevice_ctx_create(&ref, (enum AVHWDeviceType)hwType, d, NULL, opts);
    if (device) (*env)->ReleaseStringUTFChars(env, device, d);
    if (ret < 0)
        return 0;
    return (jlong)(intptr_t)ref;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_bufferRefFree(JNIEnv *env, jobject thiz, jlong ref)
{
    AVBufferRef *r = PTR(AVBufferRef *, ref);
    if (r) {
        AVBufferRef *tmp = r;
        av_buffer_unref(&tmp);
    }
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_hwframeCtxAlloc(JNIEnv *env, jobject thiz, jlong deviceRef)
{
    AVBufferRef *dev = PTR(AVBufferRef *, deviceRef);
    if (!dev)
        return 0;
    return (jlong)(intptr_t)av_hwframe_ctx_alloc(dev);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_hwframeCtxInit(JNIEnv *env, jobject thiz, jlong framesRef)
{
    AVBufferRef *r = PTR(AVBufferRef *, framesRef);
    return r ? (jint)av_hwframe_ctx_init(r) : AVERROR(EINVAL);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_hwframeTransferData(JNIEnv *env, jobject thiz,
                                                 jlong dstFrame, jlong srcFrame, jint flags)
{
    AVFrame *dst = PTR(AVFrame *, dstFrame);
    AVFrame *src = PTR(AVFrame *, srcFrame);
    if (!dst || !src)
        return AVERROR(EINVAL);
    return (jint)av_hwframe_transfer_data(dst, src, flags);
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_hwframeGetHwFramesCtx(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    if (!f || !f->hw_frames_ctx)
        return 0;
    AVBufferRef *ref = av_buffer_ref(f->hw_frames_ctx);
    return (jlong)(intptr_t)ref;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_codecGetHwFramesParameters(JNIEnv *env, jobject thiz,
                                                        jlong codecCtx, jlong deviceRef)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    AVBufferRef *dev = PTR(AVBufferRef *, deviceRef);
    if (!cc || !dev)
        return 0;
    AVBufferRef *out = NULL;
    int ret = avcodec_get_hw_frames_parameters(cc, dev, AV_PIX_FMT_NONE, &out);
    if (ret < 0 || !out)
        return 0;
    return (jlong)(intptr_t)out;
}

/* ------------------------------------------------------------------ */
/* libavutil: dict helpers, channel layout, opt getters/setters       */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_dictParseString(JNIEnv *env, jobject thiz,
                                             jlong box, jstring str, jstring keyValSep, jstring pairsSep, jint flags)
{
    AVDictionary **dst = PTR(AVDictionary **, box);
    if (!dst || !str)
        return AVERROR(EINVAL);
    const char *s = (*env)->GetStringUTFChars(env, str, NULL);
    const char *k = keyValSep ? (*env)->GetStringUTFChars(env, keyValSep, NULL) : NULL;
    const char *p = pairsSep ? (*env)->GetStringUTFChars(env, pairsSep, NULL) : NULL;
    int ret = av_dict_parse_string(dst, s, k ? k : ":", p ? p : ",", flags);
    (*env)->ReleaseStringUTFChars(env, str, s);
    if (k) (*env)->ReleaseStringUTFChars(env, keyValSep, k);
    if (p) (*env)->ReleaseStringUTFChars(env, pairsSep, p);
    return ret;
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_dictGetString(JNIEnv *env, jobject thiz, jlong box, jstring keyValSep, jstring pairsSep)
{
    AVDictionary **src = PTR(AVDictionary **, box);
    if (!src)
        return NULL;
    char *buf = NULL;
    const char *k = keyValSep ? (*env)->GetStringUTFChars(env, keyValSep, NULL) : NULL;
    const char *p = pairsSep ? (*env)->GetStringUTFChars(env, pairsSep, NULL) : NULL;
    int ret = av_dict_get_string(*src, &buf, k ? k[0] : ':', p ? p[0] : ',');
    if (k) (*env)->ReleaseStringUTFChars(env, keyValSep, k);
    if (p) (*env)->ReleaseStringUTFChars(env, pairsSep, p);
    jstring result = (ret == 0 && buf) ? (*env)->NewStringUTF(env, buf) : NULL;
    if (buf)
        av_freep(&buf);
    return result;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_dictIterate(JNIEnv *env, jobject thiz, jlong box, jlong prevHandle)
{
    AVDictionary **pp = PTR(AVDictionary **, box);
    const AVDictionaryEntry *prev = PTR(const AVDictionaryEntry *, prevHandle);
    if (!pp)
        return 0;
    return (jlong)(intptr_t)av_dict_iterate(*pp, prev);
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_dictEntryGetKey(JNIEnv *env, jobject thiz, jlong entryHandle)
{
    const AVDictionaryEntry *e = PTR(const AVDictionaryEntry *, entryHandle);
    return (e && e->key) ? (*env)->NewStringUTF(env, e->key) : NULL;
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_dictEntryGetValue(JNIEnv *env, jobject thiz, jlong entryHandle)
{
    const AVDictionaryEntry *e = PTR(const AVDictionaryEntry *, entryHandle);
    return (e && e->value) ? (*env)->NewStringUTF(env, e->value) : NULL;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_getChannelLayoutNbChannels(JNIEnv *env, jobject thiz, jlong layout)
{
    AVChannelLayout ch_layout;
    if (av_channel_layout_from_mask(&ch_layout, (uint64_t)layout) < 0)
        return 0;
    int nb = ch_layout.nb_channels;
    av_channel_layout_uninit(&ch_layout);
    return (jint)nb;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_getDefaultChannelLayout(JNIEnv *env, jobject thiz, jint nbChannels)
{
    AVChannelLayout layout = { 0 };
    av_channel_layout_default(&layout, nbChannels);
    uint64_t mask = (layout.order == AV_CHANNEL_ORDER_NATIVE) ? layout.u.mask : 0;
    av_channel_layout_uninit(&layout);
    return (jlong)mask;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_channelLayoutDefault(JNIEnv *env, jobject thiz, jlong frameOrCtx, jint nbChannels)
{
    /* Operates on an AVFrame's ch_layout. The handle doubles as a codec ctx
     * handle only in legacy code paths; here we only handle AVFrame handles. */
    AVFrame *f = PTR(AVFrame *, frameOrCtx);
    if (!f)
        return AVERROR(EINVAL);
    av_channel_layout_default(&f->ch_layout, nbChannels);
    return 0;
}

/* Drive the layout normaliser from a plain mask+count pair, for callers that
 * hold the two values rather than an AVFrame (e.g. checking up front whether a
 * stream could be normalised before any frame has been decoded).
 *
 * This entry point answers "would this layout be usable?", not "did anything
 * change?": a mask that already matches the channel count returns that count
 * (> 0) rather than 0, because the caller only wants to know whether the
 * combination is workable. A negative AVERROR means it is not: the layout is
 * invalid, the count is out of range, the two disagree, or there are more than
 * two channels with no usable mask to take the order from.
 *
 * No layout is created or returned: only the decision is reported, so there is
 * nothing for the caller to free. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_channelLayoutNormalizeMask(JNIEnv *env, jobject thiz,
                                                        jlong currentMask, jint nbChannels)
{
    AVChannelLayout cur = { 0 }, next = { 0 };
    int ret;

    if (nbChannels <= 0 || nbChannels > 64)
        return AVERROR(EINVAL);

    /* A definite mask that agrees with the channel count is already usable. */
    ret = (currentMask != 0)
          ? av_channel_layout_from_mask(&cur, (uint64_t)currentMask)
          : AVERROR(EINVAL);
    if (ret >= 0 && cur.order == AV_CHANNEL_ORDER_NATIVE &&
        cur.nb_channels == nbChannels) {
        const int n = cur.nb_channels;
        av_channel_layout_uninit(&cur);
        return (jint)n;
    }
    av_channel_layout_uninit(&cur);

    if (nbChannels > 2)
        return AVERROR(EINVAL);
    /* See the frame entry point above (frameNormalizeChannelLayout): the default is applied only when it
     * actually produced a definite layout. */
    av_channel_layout_default(&next, nbChannels);
    if (next.order != AV_CHANNEL_ORDER_NATIVE || next.nb_channels != nbChannels) {
        av_channel_layout_uninit(&next);
        return AVERROR(EINVAL);
    }
    {
        const int n = next.nb_channels;
        av_channel_layout_uninit(&next);
        return (jint)n;
    }
}


JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_optSetQ(JNIEnv *env, jobject thiz, jlong obj, jstring name, jint num, jint den)
{
    void *o = PTR(void *, obj);
    if (!o || !name)
        return AVERROR(EINVAL);
    const char *n = (*env)->GetStringUTFChars(env, name, NULL);
    AVRational q = { num, den };
    int ret = av_opt_set_video_rate(o, n, q, 0);
    (*env)->ReleaseStringUTFChars(env, name, n);
    return ret;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_optSetImageSize(JNIEnv *env, jobject thiz, jlong obj, jstring name, jint w, jint h)
{
    void *o = PTR(void *, obj);
    if (!o || !name)
        return AVERROR(EINVAL);
    const char *n = (*env)->GetStringUTFChars(env, name, NULL);
    int ret = av_opt_set_image_size(o, n, w, h, 0);
    (*env)->ReleaseStringUTFChars(env, name, n);
    return ret;
}

JNIEXPORT jdouble JNICALL
Java_org_ffmpeg_FFMpegNative_optGetDouble(JNIEnv *env, jobject thiz, jlong obj, jstring name)
{
    void *o = PTR(void *, obj);
    if (!o || !name)
        return (jdouble)AVERROR(EINVAL);
    const char *n = (*env)->GetStringUTFChars(env, name, NULL);
    double v = 0;
    int ret = av_opt_get_double(o, n, 0, &v);
    (*env)->ReleaseStringUTFChars(env, name, n);
    return (ret < 0) ? (jdouble)ret : (jdouble)v;
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_optGetString(JNIEnv *env, jobject thiz, jlong obj, jstring name)
{
    void *o = PTR(void *, obj);
    if (!o || !name)
        return NULL;
    const char *n = (*env)->GetStringUTFChars(env, name, NULL);
    uint8_t *v = NULL;
    int ret = av_opt_get(o, n, 0, &v);
    (*env)->ReleaseStringUTFChars(env, name, n);
    if (ret < 0 || !v)
        return NULL;
    jstring result = (*env)->NewStringUTF(env, (const char *)v);
    av_freep(&v);
    return result;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_compareTs(JNIEnv *env, jobject thiz,
                                       jlong tsA, jint aNum, jint aDen, jlong tsB, jint bNum, jint bDen)
{
    AVRational tba = { aNum, aDen };
    AVRational tbb = { bNum, bDen };
    return (jint)av_compare_ts((int64_t)tsA, tba, (int64_t)tsB, tbb);
}

/* ------------------------------------------------------------------ */
/* AVPacket / AVFrame / AVCodecParameters / AVStream field getters    */
/* ------------------------------------------------------------------ */

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_packetGetDuration(JNIEnv *env, jobject thiz, jlong pkt)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    return p ? (jlong)p->duration : 0;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_packetGetPos(JNIEnv *env, jobject thiz, jlong pkt)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    return p ? (jlong)p->pos : -1;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_packetSetPts(JNIEnv *env, jobject thiz, jlong pkt, jlong pts)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    if (p) p->pts = (int64_t)pts;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_packetSetDts(JNIEnv *env, jobject thiz, jlong pkt, jlong dts)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    if (p) p->dts = (int64_t)dts;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_packetSetDuration(JNIEnv *env, jobject thiz, jlong pkt, jlong duration)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    if (p) p->duration = (int64_t)duration;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_packetSetStreamIndex(JNIEnv *env, jobject thiz, jlong pkt, jint index)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    if (p) p->stream_index = index;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_packetSetFlags(JNIEnv *env, jobject thiz, jlong pkt, jint flags)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    if (p) p->flags = flags;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_packetGetSideData(JNIEnv *env, jobject thiz,
                                                jlong pkt, jint type, jbyteArray out, jint off, jint len)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    if (!p || !out)
        return -1;
    AVPacketSideData *sd = p->side_data;
    for (int i = 0; i < p->side_data_elems; i++) {
        if ((jint)sd[i].type == type) {
            jint n = (sd[i].size < len) ? sd[i].size : len;
            (*env)->SetByteArrayRegion(env, out, off, n, (const jbyte *)sd[i].data);
            return n;
        }
    }
    return -1;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetDuration(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    return f ? (jlong)f->duration : 0;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetPktPos(JNIEnv *env, jobject thiz, jlong frame)
{
    (void)env; (void)thiz; (void)frame;
    return -1;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetSarNum(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    return f ? (jint)f->sample_aspect_ratio.num : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetSarDen(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    return f ? (jint)f->sample_aspect_ratio.den : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetColorRange(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    return f ? (jint)f->color_range : (jint)AVCOL_RANGE_UNSPECIFIED;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetColorSpace(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    return f ? (jint)f->colorspace : (jint)AVCOL_SPC_UNSPECIFIED;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetColorPrimaries(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    return f ? (jint)f->color_primaries : (jint)AVCOL_PRI_UNSPECIFIED;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetColorTrc(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    return f ? (jint)f->color_trc : (jint)AVCOL_TRC_UNSPECIFIED;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetPictType(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    return f ? (jint)f->pict_type : (jint)AV_PICTURE_TYPE_NONE;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetParCodecTag(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVCodecParameters *p = stream_par(PTR(AVFormatContext *, ctx), index);
    return p ? (jint)p->codec_tag : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetParSampleRate(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVCodecParameters *p = stream_par(PTR(AVFormatContext *, ctx), index);
    return p ? (jint)p->sample_rate : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetParChannels(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVCodecParameters *p = stream_par(PTR(AVFormatContext *, ctx), index);
    return p ? (jint)p->ch_layout.nb_channels : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetParProfile(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVCodecParameters *p = stream_par(PTR(AVFormatContext *, ctx), index);
    return p ? (jint)p->profile : (jint)AV_PROFILE_UNKNOWN;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetParChannelLayout(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVCodecParameters *p = stream_par(PTR(AVFormatContext *, ctx), index);
    if (!p)
        return 0;
    /* Only mask-order layouts map onto an Android channel mask; native/custom
     * orders have no meaningful bitmask, so report 0 and let the caller fall
     * back to a channel-count-derived layout. */
    if (p->ch_layout.order != AV_CHANNEL_ORDER_NATIVE)
        return 0;
    return (jlong)p->ch_layout.u.mask;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetExtradata(JNIEnv *env, jobject thiz,
                                               jlong ctx, jint index, jbyteArray out, jint off, jint len)
{
    AVCodecParameters *p = stream_par(PTR(AVFormatContext *, ctx), index);
    if (!p || !out || !p->extradata || p->extradata_size <= 0)
        return -1;
    jint n = (p->extradata_size < len) ? p->extradata_size : len;
    (*env)->SetByteArrayRegion(env, out, off, n, (const jbyte *)p->extradata);
    return n;
}

/* Byte length of stream index's codecpar->extradata (0 when absent). Use it
 * to size the buffer for streamGetExtradata(); for a Matroska
 * AVMEDIA_TYPE_ATTACHMENT stream this is the font-file size. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetParExtradataSize(JNIEnv *env, jobject thiz,
                                                       jlong ctx, jint index)
{
    AVCodecParameters *p = stream_par(PTR(AVFormatContext *, ctx), index);
    return (p && p->extradata && p->extradata_size > 0) ? p->extradata_size : 0;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_formatGetStreamDuration(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c || index < 0 || index >= (jint)c->nb_streams)
        return (jlong)AV_NOPTS_VALUE;
    return (jlong)c->streams[index]->duration;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetDisposition(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c || index < 0 || index >= (jint)c->nb_streams)
        return 0;
    return (jint)c->streams[index]->disposition;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_streamSetDisposition(JNIEnv *env, jobject thiz, jlong ctx, jint index, jint disposition)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c || index < 0 || index >= (jint)c->nb_streams)
        return;
    c->streams[index]->disposition = disposition;
}

/* Disable codec-level parsing for a single stream. Closes any parser that
 * avformat_find_stream_info may have created (stored in st->parser) and sets
 * need_parsing to AVSTREAM_PARSE_NONE so that av_read_frame returns the raw
 * demuxed payload without parser-induced frame splitting.
 *
 * Use this for streams whose codec_id was misidentified by the demuxer (e.g.
 * AV3A audio tagged as MP3 in MPEG-TS, codec_id=86017), so that the raw PES
 * payload is passed through unmodified to the external decoder. Must be
 * called after formatFindStreamInfo and before the first readFrame. */
JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_streamDisableParsing(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c || index < 0 || index >= (jint)c->nb_streams)
        return;
    AVStream *st = c->streams[index];
    avpriv_stream_set_need_parsing(st, AVSTREAM_PARSE_NONE);
    FFStream *ffi = ffstream(st);
    if (ffi->parser) {
        av_parser_close(ffi->parser);
        ffi->parser = NULL;
    }
}

/* Copy codecpar extradata into a packet's side_data via the encoder path is
 * the caller's responsibility; here we expose codecpar extradata setting for
 * muxing setup. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_codecparSetExtradata(JNIEnv *env, jobject thiz,
                                                  jlong fmtCtx, jint index, jbyteArray in, jint off, jint len)
{
    AVCodecParameters *p = stream_par(PTR(AVFormatContext *, fmtCtx), index);
    if (!p)
        return AVERROR(EINVAL);
    if (p->extradata)
        av_freep(&p->extradata);
    p->extradata_size = 0;
    if (!in || len <= 0)
        return 0;
    p->extradata = (uint8_t *)av_mallocz(len + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!p->extradata)
        return AVERROR(ENOMEM);
    (*env)->GetByteArrayRegion(env, in, off, len, (jbyte *)p->extradata);
    p->extradata_size = len;
    return len;
}

/* Set codec-context extradata directly (decode-side init, e.g. from
 * ExoPlayer Format.initializationData). Must be called before codecOpen2. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_codecContextSetExtradata(JNIEnv *env, jobject thiz,
                                                      jlong codecCtx, jbyteArray in, jint off, jint len)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    if (!cc)
        return AVERROR(EINVAL);
    if (cc->extradata)
        av_freep(&cc->extradata);
    cc->extradata_size = 0;
    if (!in || len <= 0)
        return 0;
    cc->extradata = (uint8_t *)av_mallocz(len + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!cc->extradata)
        return AVERROR(ENOMEM);
    (*env)->GetByteArrayRegion(env, in, off, len, (jbyte *)cc->extradata);
    cc->extradata_size = len;
    return len;
}

/* ------------------------------------------------------------------ */
/* libswscale: extra scaling + colorspace                              */
/* ------------------------------------------------------------------ */

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_swsGetCachedContext(JNIEnv *env, jobject thiz, jlong prev,
                                                 jint srcW, jint srcH, jint srcFmt,
                                                 jint dstW, jint dstH, jint dstFmt, jint flags)
{
    SwsContext *p = PTR(SwsContext *, prev);
    return (jlong)(intptr_t)sws_getCachedContext(p, srcW, srcH, (enum AVPixelFormat)srcFmt,
                                                dstW, dstH, (enum AVPixelFormat)dstFmt,
                                                flags, NULL, NULL, NULL);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_swsInitContext(JNIEnv *env, jobject thiz, jlong sws)
{
    struct SwsContext *c = PTR(struct SwsContext *, sws);
    return c ? (jint)sws_init_context(c, NULL, NULL) : AVERROR(EINVAL);
}

/* Scale frame-to-frame: dst must be allocated with format/width/height set.
 * sws_scale_frame() handles allocation/updating based on src frame. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_swsScaleFrameCtx(JNIEnv *env, jobject thiz, jlong sws, jlong dstFrame, jlong srcFrame)
{
    struct SwsContext *c = PTR(struct SwsContext *, sws);
    AVFrame *dst = PTR(AVFrame *, dstFrame);
    AVFrame *src = PTR(AVFrame *, srcFrame);
    if (!c || !dst || !src)
        return AVERROR(EINVAL);
    return (jint)sws_scale_frame(c, dst, src);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_swsSetColorspaceDetails(JNIEnv *env, jobject thiz,
                                                     jlong sws, jintArray invTable, jint srcRange,
                                                     jintArray table, jint dstRange,
                                                     jint brightness, jint contrast, jint saturation)
{
    struct SwsContext *c = PTR(struct SwsContext *, sws);
    if (!c || !invTable || !table)
        return AVERROR(EINVAL);
    int inv[4], tab[4];
    (*env)->GetIntArrayRegion(env, invTable, 0, 4, inv);
    (*env)->GetIntArrayRegion(env, table, 0, 4, tab);
    return (jint)sws_setColorspaceDetails(c, inv, srcRange, tab, dstRange,
                                          brightness, contrast, saturation);
}

/* ------------------------------------------------------------------ */
/* libswresample: convert_frame, next_pts, compensation, silence       */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_swrConvertFrame(JNIEnv *env, jobject thiz,
                                             jlong ctx, jlong outFrame, jlong inFrame)
{
    struct SwrContext *c = PTR(struct SwrContext *, ctx);
    AVFrame *out = outFrame ? PTR(AVFrame *, outFrame) : NULL;
    AVFrame *in  = inFrame  ? PTR(AVFrame *, inFrame)  : NULL;
    if (!c)
        return AVERROR(EINVAL);
    return (jint)swr_convert_frame(c, out, in);
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_swrNextPts(JNIEnv *env, jobject thiz, jlong ctx, jlong pts)
{
    struct SwrContext *c = PTR(struct SwrContext *, ctx);
    return c ? (jlong)swr_next_pts(c, (int64_t)pts) : pts;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_swrSetCompensation(JNIEnv *env, jobject thiz,
                                                 jlong ctx, jint sampleDelta, jint compensationDistance)
{
    struct SwrContext *c = PTR(struct SwrContext *, ctx);
    return c ? (jint)swr_set_compensation(c, sampleDelta, compensationDistance) : AVERROR(EINVAL);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_swrInjectSilence(JNIEnv *env, jobject thiz, jlong ctx, jint count)
{
    struct SwrContext *c = PTR(struct SwrContext *, ctx);
    return c ? (jint)swr_inject_silence(c, count) : AVERROR(EINVAL);
}

/* ------------------------------------------------------------------ */
/* libavfilter: flags variants, sink info, commands, pads              */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_bufferSrcAddFrameFlags(JNIEnv *env, jobject thiz,
                                                    jlong srcCtx, jlong frame, jint flags)
{
    AVFilterContext *c = PTR(AVFilterContext *, srcCtx);
    AVFrame *f = frame ? PTR(AVFrame *, frame) : NULL;
    if (!c)
        return AVERROR(EINVAL);
    return (jint)av_buffersrc_add_frame_flags(c, f, flags);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_bufferSinkGetFrameFlags(JNIEnv *env, jobject thiz,
                                                     jlong sinkCtx, jlong frame, jint flags)
{
    AVFilterContext *c = PTR(AVFilterContext *, sinkCtx);
    AVFrame *f = PTR(AVFrame *, frame);
    if (!c || !f)
        return AVERROR(EINVAL);
    return (jint)av_buffersink_get_frame_flags(c, f, flags);
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_bufferSinkSetFrameSize(JNIEnv *env, jobject thiz, jlong sinkCtx, jint frameSize)
{
    AVFilterContext *c = PTR(AVFilterContext *, sinkCtx);
    if (c)
        av_buffersink_set_frame_size(c, (unsigned)frameSize);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_bufferSinkGetFrameRateNum(JNIEnv *env, jobject thiz, jlong sinkCtx)
{
    AVFilterContext *c = PTR(AVFilterContext *, sinkCtx);
    return c ? (jint)av_buffersink_get_frame_rate(c).num : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_bufferSinkGetFrameRateDen(JNIEnv *env, jobject thiz, jlong sinkCtx)
{
    AVFilterContext *c = PTR(AVFilterContext *, sinkCtx);
    return c ? (jint)av_buffersink_get_frame_rate(c).den : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_bufferSinkGetTimeBaseNum(JNIEnv *env, jobject thiz, jlong sinkCtx)
{
    AVFilterContext *c = PTR(AVFilterContext *, sinkCtx);
    return c ? (jint)av_buffersink_get_time_base(c).num : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_bufferSinkGetTimeBaseDen(JNIEnv *env, jobject thiz, jlong sinkCtx)
{
    AVFilterContext *c = PTR(AVFilterContext *, sinkCtx);
    return c ? (jint)av_buffersink_get_time_base(c).den : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_filterGraphRequestOldest(JNIEnv *env, jobject thiz, jlong graph)
{
    AVFilterGraph *g = PTR(AVFilterGraph *, graph);
    return g ? (jint)avfilter_graph_request_oldest(g) : AVERROR(EINVAL);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_filterGraphSendCommand(JNIEnv *env, jobject thiz,
                                                    jlong graph, jstring target, jstring cmd, jstring arg,
                                                    jbyteArray resOut, jint resLen, jint flags)
{
    AVFilterGraph *g = PTR(AVFilterGraph *, graph);
    if (!g || !target || !cmd || !arg)
        return AVERROR(EINVAL);
    const char *t = (*env)->GetStringUTFChars(env, target, NULL);
    const char *c = (*env)->GetStringUTFChars(env, cmd, NULL);
    const char *a = (*env)->GetStringUTFChars(env, arg, NULL);
    char *resbuf = NULL;
    int alloc = resOut && resLen > 0 ? resLen : 0;
    if (alloc > 0)
        resbuf = (char *)calloc(1, alloc);
    int ret = avfilter_graph_send_command(g, t, c, a, resbuf, alloc, flags);
    (*env)->ReleaseStringUTFChars(env, target, t);
    (*env)->ReleaseStringUTFChars(env, cmd, c);
    (*env)->ReleaseStringUTFChars(env, arg, a);
    if (resbuf) {
        int n = strlen(resbuf);
        if (n > alloc) n = alloc;
        (*env)->SetByteArrayRegion(env, resOut, 0, n, (const jbyte *)resbuf);
        free(resbuf);
    }
    return ret;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_filterGraphQueueCommand(JNIEnv *env, jobject thiz,
                                                     jlong graph, jstring target, jstring cmd, jstring arg,
                                                     jint flags, jdouble ts)
{
    AVFilterGraph *g = PTR(AVFilterGraph *, graph);
    if (!g || !target || !cmd || !arg)
        return AVERROR(EINVAL);
    const char *t = (*env)->GetStringUTFChars(env, target, NULL);
    const char *c = (*env)->GetStringUTFChars(env, cmd, NULL);
    const char *a = (*env)->GetStringUTFChars(env, arg, NULL);
    int ret = avfilter_graph_queue_command(g, t, c, a, flags, (double)ts);
    (*env)->ReleaseStringUTFChars(env, target, t);
    (*env)->ReleaseStringUTFChars(env, cmd, c);
    (*env)->ReleaseStringUTFChars(env, arg, a);
    return ret;
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_filterGraphDump(JNIEnv *env, jobject thiz, jlong graph, jstring options)
{
    AVFilterGraph *g = PTR(AVFilterGraph *, graph);
    if (!g)
        return NULL;
    const char *o = options ? (*env)->GetStringUTFChars(env, options, NULL) : NULL;
    char *s = avfilter_graph_dump(g, o);
    if (options) (*env)->ReleaseStringUTFChars(env, options, o);
    jstring result = s ? (*env)->NewStringUTF(env, s) : NULL;
    if (s)
        av_free(s);
    return result;
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_filterPadGetName(JNIEnv *env, jobject thiz, jlong pads, jint idx)
{
    const AVFilterPad *p = PTR(const AVFilterPad *, pads);
    if (!p)
        return NULL;
    const char *n = avfilter_pad_get_name(p, idx);
    return n ? (*env)->NewStringUTF(env, n) : NULL;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_filterPadGetType(JNIEnv *env, jobject thiz, jlong pads, jint idx)
{
    const AVFilterPad *p = PTR(const AVFilterPad *, pads);
    return p ? (jint)avfilter_pad_get_type(p, idx) : (jint)AVMEDIA_TYPE_UNKNOWN;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_filterGetNbInputs(JNIEnv *env, jobject thiz, jlong filterCtx)
{
    AVFilterContext *c = PTR(AVFilterContext *, filterCtx);
    return c ? (jint)c->nb_inputs : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_filterGetNbOutputs(JNIEnv *env, jobject thiz, jlong filterCtx)
{
    AVFilterContext *c = PTR(AVFilterContext *, filterCtx);
    return c ? (jint)c->nb_outputs : 0;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_filterGetInputPads(JNIEnv *env, jobject thiz, jlong filterCtx)
{
    AVFilterContext *c = PTR(AVFilterContext *, filterCtx);
    return c ? (jlong)(intptr_t)c->input_pads : 0;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_filterGetOutputPads(JNIEnv *env, jobject thiz, jlong filterCtx)
{
    AVFilterContext *c = PTR(AVFilterContext *, filterCtx);
    return c ? (jlong)(intptr_t)c->output_pads : 0;
}

/* ------------------------------------------------------------------ */
/* Hardware device type lookups (name <-> enum string)                  */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_hwDeviceFindTypeByName(JNIEnv *env, jobject thiz, jstring name)
{
    if (!name)
        return (jint)AV_HWDEVICE_TYPE_NONE;
    const char *n = (*env)->GetStringUTFChars(env, name, NULL);
    enum AVHWDeviceType t = av_hwdevice_find_type_by_name(n);
    (*env)->ReleaseStringUTFChars(env, name, n);
    return (jint)t;
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_hwDeviceGetTypeName(JNIEnv *env, jobject thiz, jint hwType)
{
    const char *n = av_hwdevice_get_type_name((enum AVHWDeviceType)hwType);
    return n ? (*env)->NewStringUTF(env, n) : NULL;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_hwDeviceIterateTypes(JNIEnv *env, jobject thiz, jint prev)
{
    return (jint)av_hwdevice_iterate_types((enum AVHWDeviceType)prev);
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_buffersrcParametersAlloc(JNIEnv *env, jobject thiz, jlong srcCtx)
{
    return (jlong)(intptr_t)av_buffersrc_parameters_alloc();
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_buffersrcParametersSet(JNIEnv *env, jobject thiz, jlong srcCtx, jlong params)
{
    AVFilterContext *c = PTR(AVFilterContext *, srcCtx);
    AVBufferSrcParameters *p = PTR(AVBufferSrcParameters *, params);
    if (!c || !p)
        return AVERROR(EINVAL);
    return (jint)av_buffersrc_parameters_set(c, p);
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_bufferSinkGetHwFramesCtx(JNIEnv *env, jobject thiz, jlong sinkCtx)
{
    AVFilterContext *c = PTR(AVFilterContext *, sinkCtx);
    if (!c)
        return 0;
    AVBufferRef *stored = av_buffersink_get_hw_frames_ctx(c);
    return stored ? (jlong)(intptr_t)av_buffer_ref(stored) : 0;
}

/* ================================================================== */
/* Round 4: gap closure (avformat/avcodec/avutil/avfilter/         */
/*          swscale/swresample + struct field access)                 */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* libavformat: pause/play, options, index, sdp, tag tables            */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_readPause(JNIEnv *env, jobject thiz, jlong ctx)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    return c ? (jint)av_read_pause(c) : AVERROR(EINVAL);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_readPlay(JNIEnv *env, jobject thiz, jlong ctx)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    return c ? (jint)av_read_play(c) : AVERROR(EINVAL);
}

/* variant of formatFindStreamInfo that accepts an options dictionary */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_formatFindStreamInfoOpts(JNIEnv *env, jobject thiz,
                                                      jlong ctx, jlong optionsBox)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c)
        return AVERROR(EINVAL);
    AVDictionary **opts = optionsBox ? PTR(AVDictionary **, optionsBox) : NULL;
    return (jint)avformat_find_stream_info(c, opts);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_indexSearchTimestamp(JNIEnv *env, jobject thiz,
                                                  jlong ctx, jint streamIndex, jlong timestamp, jint flags)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c || streamIndex < 0 || streamIndex >= (jint)c->nb_streams)
        return AVERROR(EINVAL);
    return (jint)av_index_search_timestamp(c->streams[streamIndex],
                                           (int64_t)timestamp, flags);
}

/* returns 0 on success; writes the SDP text into `out` */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_sdpCreate(JNIEnv *env, jobject thiz, jlong ctx,
                                       jbyteArray out, jint off, jint len)
{
    if (!ctx || !out)
        return AVERROR(EINVAL);
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    AVFormatContext *ac[1] = { c };
    char *buf = (char *)av_malloc((size_t)len);
    if (!buf)
        return AVERROR(ENOMEM);
    int ret = av_sdp_create(ac, 1, buf, len);
    if (ret >= 0) {
        size_t n = strlen(buf);
        if (n <= (size_t)len)
            (*env)->SetByteArrayRegion(env, out, off, (jsize)n, (const jbyte *)buf);
    }
    av_free(buf);
    return ret;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_getRiffVideoTags(JNIEnv *env, jobject thiz)
{
    return (jlong)(intptr_t)avformat_get_riff_video_tags();
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_getRiffAudioTags(JNIEnv *env, jobject thiz)
{
    return (jlong)(intptr_t)avformat_get_riff_audio_tags();
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_getMovVideoTags(JNIEnv *env, jobject thiz)
{
    return (jlong)(intptr_t)avformat_get_mov_video_tags();
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_getMovAudioTags(JNIEnv *env, jobject thiz)
{
    return (jlong)(intptr_t)avformat_get_mov_audio_tags();
}

/* tag <-> codec id lookups over an AVCodecTag table handle */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_codecGetIdFromTag(JNIEnv *env, jobject thiz, jlong tagTable, jint tag)
{
    const struct AVCodecTag *t = PTR(const struct AVCodecTag *, tagTable);
    if (!t)
        return AV_CODEC_ID_NONE;
    const struct AVCodecTag *const table[2] = { t, NULL };
    return (jint)av_codec_get_id(table, (unsigned int)tag);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_codecGetTagFromId(JNIEnv *env, jobject thiz, jlong tagTable, jint codecId)
{
    const struct AVCodecTag *t = PTR(const struct AVCodecTag *, tagTable);
    if (!t)
        return 0;
    const struct AVCodecTag *const table[2] = { t, NULL };
    return (jint)av_codec_get_tag(table, (enum AVCodecID)codecId);
}

/* ------------------------------------------------------------------ */
/* libavcodec: iterate, supported-config, best pix fmt, bits, mem      */
/* ------------------------------------------------------------------ */

/* iterate codec handles; pass a long[] whose [0] holds the opaque iter state
 * (0 to start) and receives the updated state. Returns an AVCodec handle,
 * or 0 at the end of the list. Thread-safe: each caller owns its own state. */
JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_codecIterate(JNIEnv *env, jobject thiz, jlongArray opaque)
{
    if (!opaque)
        return 0;
    jlong *e = (*env)->GetLongArrayElements(env, opaque, NULL);
    if (!e)
        return 0;
    void *iter = e[0] ? PTR(void *, e[0]) : NULL;
    const AVCodec *c = av_codec_iterate(&iter);
    e[0] = (jlong)(intptr_t)iter;
    (*env)->ReleaseLongArrayElements(env, opaque, e, 0);
    return (jlong)(intptr_t)c;
}

/* Return the compact enum lists for PIX_FORMAT / SAMPLE_RATE /
 * SAMPLE_FORMAT / COLOR_RANGE / COLOR_SPACE / ALPHA_MODE configs as a
 * single int[] prefixed by (config, count). Returns NULL for
 * FRAME_RATE / CHANNEL_LAYOUT (complex element types) or on error. */
JNIEXPORT jintArray JNICALL
Java_org_ffmpeg_FFMpegNative_codecGetSupportedConfigs(JNIEnv *env, jobject thiz,
                                                      jlong codecCtx, jlong codec,
                                                      jint config, jint flags)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    AVCodec *co = PTR(AVCodec *, codec);
    const void *configs = NULL;
    int num = 0;
    int ret = avcodec_get_supported_config(cc, co, (enum AVCodecConfig)config,
                                           (unsigned)flags, &configs, &num);
    if (ret < 0 || !configs || num <= 0)
        return NULL;
    jintArray arr = (*env)->NewIntArray(env, 2 + num);
    if (!arr)
        return NULL;
    jint *vals = (*env)->GetIntArrayElements(env, arr, NULL);
    if (!vals)
        return NULL;
    vals[0] = (jint)config;
    vals[1] = (jint)num;
    switch ((enum AVCodecConfig)config) {
    case AV_CODEC_CONFIG_PIX_FORMAT:
        for (int i = 0; i < num; i++)
            vals[2 + i] = (jint)((const enum AVPixelFormat *)configs)[i];
        break;
    case AV_CODEC_CONFIG_SAMPLE_RATE:
        for (int i = 0; i < num; i++)
            vals[2 + i] = (jint)((const int *)configs)[i];
        break;
    case AV_CODEC_CONFIG_SAMPLE_FORMAT:
        for (int i = 0; i < num; i++)
            vals[2 + i] = (jint)((const enum AVSampleFormat *)configs)[i];
        break;
    case AV_CODEC_CONFIG_COLOR_RANGE:
        for (int i = 0; i < num; i++)
            vals[2 + i] = (jint)((const enum AVColorRange *)configs)[i];
        break;
    case AV_CODEC_CONFIG_COLOR_SPACE:
        for (int i = 0; i < num; i++)
            vals[2 + i] = (jint)((const enum AVColorSpace *)configs)[i];
        break;
    case AV_CODEC_CONFIG_ALPHA_MODE:
        for (int i = 0; i < num; i++)
            vals[2 + i] = (jint)((const enum AVAlphaMode *)configs)[i];
        break;
    default:
        (*env)->ReleaseIntArrayElements(env, arr, vals, JNI_ABORT);
        (*env)->DeleteLocalRef(env, arr);
        return NULL; /* FRAME_RATE / CHANNEL_LAYOUT not expressible as int */
    }
    (*env)->ReleaseIntArrayElements(env, arr, vals, 0);
    return arr;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_codecFindBestPixFmtOfList(JNIEnv *env, jobject thiz,
                                                       jintArray pixFmtList, jint srcPixFmt,
                                                       jint hasAlpha, jintArray lossOut)
{
    if (!pixFmtList)
        return (jint)AV_PIX_FMT_NONE;
    jsize n = (*env)->GetArrayLength(env, pixFmtList);
    jint elems[64];
    if (n > 64) n = 64;
    if (n <= 0)
        return (jint)AV_PIX_FMT_NONE;
    (*env)->GetIntArrayRegion(env, pixFmtList, 0, n, elems);
    enum AVPixelFormat list[65];
    for (jsize i = 0; i < n; i++)
        list[i] = (enum AVPixelFormat)elems[i];
    list[n] = AV_PIX_FMT_NONE;
    int loss = 0;
    enum AVPixelFormat best = avcodec_find_best_pix_fmt_of_list(list,
                                        (enum AVPixelFormat)srcPixFmt, hasAlpha, &loss);
    if (lossOut) {
        jsize l = (*env)->GetArrayLength(env, lossOut);
        if (l >= 1) {
            jint *lo = (*env)->GetIntArrayElements(env, lossOut, NULL);
            if (lo) {
                lo[0] = loss;
                (*env)->ReleaseIntArrayElements(env, lossOut, lo, 0);
            }
        }
    }
    return (jint)best;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_getBitsPerSample(JNIEnv *env, jobject thiz, jint codecId)
{
    return (jint)av_get_bits_per_sample((enum AVCodecID)codecId);
}

/* Fast padded allocation for codec extradata etc. The buffer is 0-initialized
 * and carries the standard FFmpeg padding, so it works directly with
 * avcodec_parameters_set_extradata() consumers. Free with free(long). */
JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_fastPaddedMalloc(JNIEnv *env, jobject thiz, jlong size)
{
    if (size < 0)
        return 0;
    size_t min_size = (size_t)size;
    return (jlong)(intptr_t)av_malloc(min_size + AV_INPUT_BUFFER_PADDING_SIZE);
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_fastPaddedMallocz(JNIEnv *env, jobject thiz, jlong size)
{
    if (size < 0)
        return 0;
    size_t min_size = (size_t)size;
    return (jlong)(intptr_t)av_mallocz(min_size + AV_INPUT_BUFFER_PADDING_SIZE);
}

/* ------------------------------------------------------------------ */
/* libavutil: samples_alloc, fifo, opt helpers, dict int, mem          */
/* ------------------------------------------------------------------ */

/* Allocate a samples buffer; copies the (packed) bytes back into `out`.
 * For planar formats only the first plane is copied back, matching the
 * frameCopyAudio() convention. Every plane is freed before returning. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_samplesAlloc(JNIEnv *env, jobject thiz,
                                          jint nbChannels, jint nbSamples, jint sampleFmt, jint align,
                                          jbyteArray out, jint off, jint len)
{
    uint8_t *data[AV_NUM_DATA_POINTERS] = {0};
    int linesize = 0;
    int ret = av_samples_alloc(data, &linesize, nbChannels, nbSamples,
                               (enum AVSampleFormat)sampleFmt, align);
    if (ret < 0)
        return ret;
    int needed = av_samples_get_buffer_size(NULL, nbChannels, nbSamples,
                                            (enum AVSampleFormat)sampleFmt, align);
    if (needed > 0 && out && needed <= len)
        (*env)->SetByteArrayRegion(env, out, off, needed, (const jbyte *)data[0]);
    for (int i = 0; i < AV_NUM_DATA_POINTERS; i++)
        av_freep(&data[i]);
    return needed;
}

/* Allocate samples + data pointer array; returns handle to pointer array
 * (free with samplesFree). For planar formats layout is ch-major. */
JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_samplesAllocArray(JNIEnv *env, jobject thiz,
                                               jint nbChannels, jint nbSamples, jint sampleFmt, jint align)
{
    uint8_t **data = NULL;
    int linesize = 0;
    int ret = av_samples_alloc_array_and_samples(&data, &linesize,
                                                 nbChannels, nbSamples,
                                                 (enum AVSampleFormat)sampleFmt, align);
    if (ret < 0)
        return 0;
    return (jlong)(intptr_t)data;
}

/* Free all planes + the pointer array. Unused trailing pointers are NULL and
 * av_freep() on them is a no-op, so iterating the whole array is safe. */
JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_samplesFree(JNIEnv *env, jobject thiz, jlong dataArray)
{
    uint8_t **data = PTR(uint8_t **, dataArray);
    if (data) {
        for (int i = 0; i < AV_NUM_DATA_POINTERS; i++)
            av_freep(&data[i]);
        av_freep(&data);
    }
}

/* byte-oriented auto-grow FIFO (libavutil/fifo.h, elem_size=1) */
JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_fifoAlloc2(JNIEnv *env, jobject thiz, jlong bytes)
{
    if (bytes < 0)
        return 0;
    return (jlong)(intptr_t)av_fifo_alloc2((size_t)bytes, 1, AV_FIFO_FLAG_AUTO_GROW);
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_fifoFree(JNIEnv *env, jobject thiz, jlong fifo)
{
    AVFifo *f = PTR(AVFifo *, fifo);
    if (f)
        av_fifo_freep2(&f);
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_fifoCanRead(JNIEnv *env, jobject thiz, jlong fifo)
{
    AVFifo *f = PTR(AVFifo *, fifo);
    return f ? (jlong)av_fifo_can_read(f) : 0;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_fifoCanWrite(JNIEnv *env, jobject thiz, jlong fifo)
{
    AVFifo *f = PTR(AVFifo *, fifo);
    return f ? (jlong)av_fifo_can_write(f) : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_fifoWrite(JNIEnv *env, jobject thiz, jlong fifo,
                                       jbyteArray in, jint off, jint len)
{
    AVFifo *f = PTR(AVFifo *, fifo);
    if (!f || !in || len < 0 || off < 0)
        return AVERROR(EINVAL);
    if (len == 0)
        return 0;
    jbyte *buf = (*env)->GetByteArrayElements(env, in, NULL);
    if (!buf)
        return AVERROR(ENOMEM);
    int ret = av_fifo_write(f, (const void *)(buf + off), (size_t)len);
    (*env)->ReleaseByteArrayElements(env, in, buf, JNI_ABORT);
    return ret;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_fifoRead(JNIEnv *env, jobject thiz, jlong fifo,
                                      jbyteArray out, jint off, jint len)
{
    AVFifo *f = PTR(AVFifo *, fifo);
    if (!f || !out || len < 0 || off < 0)
        return AVERROR(EINVAL);
    if (len == 0)
        return 0;
    size_t avail = av_fifo_can_read(f);
    if ((size_t)len > avail)
        len = (jint)avail;
    if (len <= 0)
        return 0;
    jbyte *buf = (*env)->GetByteArrayElements(env, out, NULL);
    if (!buf)
        return AVERROR(ENOMEM);
    int ret = av_fifo_read(f, (void *)(buf + off), (size_t)len);
    (*env)->ReleaseByteArrayElements(env, out, buf, 0);
    return (ret < 0) ? ret : len;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_fifoDrain(JNIEnv *env, jobject thiz, jlong fifo, jlong n)
{
    AVFifo *f = PTR(AVFifo *, fifo);
    if (f && n > 0)
        av_fifo_drain2(f, (size_t)n);
}

/* av_dict_set_int */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_dictSetInt(JNIEnv *env, jobject thiz,
                                        jlong box, jstring key, jlong value, jint flags)
{
    AVDictionary **pp = PTR(AVDictionary **, box);
    if (!pp || !key)
        return AVERROR(EINVAL);
    const char *k = (*env)->GetStringUTFChars(env, key, NULL);
    int ret = av_dict_set_int(pp, k, (int64_t)value, flags);
    (*env)->ReleaseStringUTFChars(env, key, k);
    return ret;
}

/* AVOption helpers */
JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_optNext(JNIEnv *env, jobject thiz, jlong obj, jlong prev)
{
    void *o = PTR(void *, obj);
    const AVOption *p = PTR(const AVOption *, prev);
    return (jlong)(intptr_t)av_opt_next(o, p);
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_optFind(JNIEnv *env, jobject thiz, jlong obj,
                                     jstring name, jstring unit, jint optFlags, jint searchFlags)
{
    void *o = PTR(void *, obj);
    if (!o || !name)
        return 0;
    const char *n = (*env)->GetStringUTFChars(env, name, NULL);
    const char *u = unit ? (*env)->GetStringUTFChars(env, unit, NULL) : NULL;
    const AVOption *opt = av_opt_find(o, n, u, optFlags, searchFlags);
    if (unit) (*env)->ReleaseStringUTFChars(env, unit, u);
    (*env)->ReleaseStringUTFChars(env, name, n);
    return (jlong)(intptr_t)opt;
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_optGetName(JNIEnv *env, jobject thiz, jlong opt)
{
    const AVOption *o = PTR(const AVOption *, opt);
    return (o && o->name) ? (*env)->NewStringUTF(env, o->name) : NULL;
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_optGetHelp(JNIEnv *env, jobject thiz, jlong opt)
{
    const AVOption *o = PTR(const AVOption *, opt);
    return (o && o->help) ? (*env)->NewStringUTF(env, o->help) : NULL;
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_optGetUnit(JNIEnv *env, jobject thiz, jlong opt)
{
    const AVOption *o = PTR(const AVOption *, opt);
    return (o && o->unit) ? (*env)->NewStringUTF(env, o->unit) : NULL;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_optGetType(JNIEnv *env, jobject thiz, jlong opt)
{
    const AVOption *o = PTR(const AVOption *, opt);
    return o ? (jint)o->type : -1;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_optSetBin(JNIEnv *env, jobject thiz, jlong obj,
                                       jstring name, jbyteArray in, jint off, jint len, jint searchFlags)
{
    void *o = PTR(void *, obj);
    if (!o || !name)
        return AVERROR(EINVAL);
    const char *n = (*env)->GetStringUTFChars(env, name, NULL);
    uint8_t *tmp = NULL;
    int ret;
    if (in && len > 0) {
        tmp = (uint8_t *)av_malloc((size_t)len);
        if (!tmp) {
            (*env)->ReleaseStringUTFChars(env, name, n);
            return AVERROR(ENOMEM);
        }
        (*env)->GetByteArrayRegion(env, in, off, len, (jbyte *)tmp);
        ret = av_opt_set_bin(o, n, tmp, len, searchFlags);
        av_freep(&tmp);
    } else {
        ret = av_opt_set_bin(o, n, NULL, 0, searchFlags);
    }
    (*env)->ReleaseStringUTFChars(env, name, n);
    return ret;
}

/* av_opt_set_dict: consumes (empties) the box */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_optSetDict(JNIEnv *env, jobject thiz, jlong obj, jlong optionsBox)
{
    void *o = PTR(void *, obj);
    AVDictionary **opts = optionsBox ? PTR(AVDictionary **, optionsBox) : NULL;
    if (!o)
        return AVERROR(EINVAL);
    return (jint)av_opt_set_dict(o, opts);
}

/* av_opt_copy: dest must be allocated (e.g. av_mallocz) but uninitialized */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_optCopy(JNIEnv *env, jobject thiz, jlong dst, jlong src)
{
    void *d = PTR(void *, dst);
    void *s = PTR(void *, src);
    if (!d || !s)
        return AVERROR(EINVAL);
    return (jint)av_opt_copy(d, s);
}

/* packed/planar sample format conversion helpers */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_getPackedSampleFmt(JNIEnv *env, jobject thiz, jint sampleFmt)
{
    return (jint)av_get_packed_sample_fmt((enum AVSampleFormat)sampleFmt);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_getPlanarSampleFmt(JNIEnv *env, jobject thiz, jint sampleFmt)
{
    return (jint)av_get_planar_sample_fmt((enum AVSampleFormat)sampleFmt);
}

/* generic memory helpers */
JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_malloc(JNIEnv *env, jobject thiz, jlong size)
{
    return size >= 0 ? (jlong)(intptr_t)av_malloc((size_t)size) : 0;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_mallocz(JNIEnv *env, jobject thiz, jlong size)
{
    return size >= 0 ? (jlong)(intptr_t)av_mallocz((size_t)size) : 0;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_free(JNIEnv *env, jobject thiz, jlong ptr)
{
    void *p = PTR(void *, ptr);
    if (p)
        av_free(p);
}

/* ------------------------------------------------------------------ */
/* libavfilter: graph_alloc_filter, init, link channels, sink usage    */
/* ------------------------------------------------------------------ */

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_filterGraphAllocFilter(JNIEnv *env, jobject thiz,
                                                    jlong graph, jlong filter, jstring name)
{
    AVFilterGraph *g = PTR(AVFilterGraph *, graph);
    const AVFilter *f = PTR(const AVFilter *, filter);
    if (!g || !f)
        return 0;
    const char *n = name ? (*env)->GetStringUTFChars(env, name, NULL) : NULL;
    AVFilterContext *ctx = avfilter_graph_alloc_filter(g, f, n);
    if (name) (*env)->ReleaseStringUTFChars(env, name, n);
    return (jlong)(intptr_t)ctx;
}

/* Pair for the above: initialize an un-initialized filter with a dict */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_filterInitDict(JNIEnv *env, jobject thiz, jlong ctx, jlong optionsBox)
{
    AVFilterContext *c = PTR(AVFilterContext *, ctx);
    if (!c)
        return AVERROR(EINVAL);
    AVDictionary **opts = optionsBox ? PTR(AVDictionary **, optionsBox) : NULL;
    return (jint)avfilter_init_dict(c, opts);
}

/* nb_channels of the channel layout negotiated on a link */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_filterLinkGetChannels(JNIEnv *env, jobject thiz, jlong link)
{
    AVFilterLink *l = PTR(AVFilterLink *, link);
    return l ? (jint)l->ch_layout.nb_channels : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_bufferSinkGetSamples(JNIEnv *env, jobject thiz,
                                                  jlong sinkCtx, jlong frame, jint nbSamples)
{
    AVFilterContext *c = PTR(AVFilterContext *, sinkCtx);
    AVFrame *f = PTR(AVFrame *, frame);
    if (!c || !f)
        return AVERROR(EINVAL);
    return (jint)av_buffersink_get_samples(c, f, nbSamples);
}

/* extra buffersink accessors (parallel to the frame-rate/time-base ones) */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_bufferSinkGetW(JNIEnv *env, jobject thiz, jlong sinkCtx)
{
    AVFilterContext *c = PTR(AVFilterContext *, sinkCtx);
    return c ? (jint)av_buffersink_get_w(c) : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_bufferSinkGetH(JNIEnv *env, jobject thiz, jlong sinkCtx)
{
    AVFilterContext *c = PTR(AVFilterContext *, sinkCtx);
    return c ? (jint)av_buffersink_get_h(c) : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_bufferSinkGetSampleAspectNum(JNIEnv *env, jobject thiz, jlong sinkCtx)
{
    AVFilterContext *c = PTR(AVFilterContext *, sinkCtx);
    return c ? (jint)av_buffersink_get_sample_aspect_ratio(c).num : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_bufferSinkGetSampleAspectDen(JNIEnv *env, jobject thiz, jlong sinkCtx)
{
    AVFilterContext *c = PTR(AVFilterContext *, sinkCtx);
    return c ? (jint)av_buffersink_get_sample_aspect_ratio(c).den : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_bufferSinkGetChannels(JNIEnv *env, jobject thiz, jlong sinkCtx)
{
    AVFilterContext *c = PTR(AVFilterContext *, sinkCtx);
    return c ? (jint)av_buffersink_get_channels(c) : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_bufferSinkGetSampleRate(JNIEnv *env, jobject thiz, jlong sinkCtx)
{
    AVFilterContext *c = PTR(AVFilterContext *, sinkCtx);
    return c ? (jint)av_buffersink_get_sample_rate(c) : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_bufferSinkGetType(JNIEnv *env, jobject thiz, jlong sinkCtx)
{
    AVFilterContext *c = PTR(AVFilterContext *, sinkCtx);
    return c ? (jint)av_buffersink_get_type(c) : (jint)AVMEDIA_TYPE_UNKNOWN;
}

/* number of failed intra-reordering requests (push mode) */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_bufferSrcGetNbFailedRequests(JNIEnv *env, jobject thiz, jlong srcCtx)
{
    AVFilterContext *c = PTR(AVFilterContext *, srcCtx);
    return c ? (jint)av_buffersrc_get_nb_failed_requests(c) : 0;
}

/* ------------------------------------------------------------------ */
/* libswscale: getColorspaceDetails, palette conversions               */
/* ------------------------------------------------------------------ */

/* Read-only snapshot: copies the 4-element inv/table arrays back into the
 * Java out arrays; returns 0 on success, negative error otherwise. The
 * arrays are owned by libswscale and must not be freed by the caller. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_swsGetColorspaceDetails(JNIEnv *env, jobject thiz, jlong sws,
                                                     jintArray invOut, jintArray srcRangeOut,
                                                     jintArray tableOut, jintArray dstRangeOut,
                                                     jintArray brightnessOut, jintArray contrastOut,
                                                     jintArray saturationOut)
{
    struct SwsContext *c = PTR(struct SwsContext *, sws);
    if (!c)
        return AVERROR(EINVAL);
    int *inv = NULL, *tab = NULL;
    int srcRange = 0, dstRange = 0, brightness = 0, contrast = 0, saturation = 0;
    int ret = sws_getColorspaceDetails(c, &inv, &srcRange, &tab, &dstRange,
                                       &brightness, &contrast, &saturation);
    if (ret < 0)
        return ret;
    if (invOut && inv) {
        jint *iv = (*env)->GetIntArrayElements(env, invOut, NULL);
        if (iv) { for (int i = 0; i < 4; i++) iv[i] = inv[i]; (*env)->ReleaseIntArrayElements(env, invOut, iv, 0); }
    }
    if (tableOut && tab) {
        jint *tb = (*env)->GetIntArrayElements(env, tableOut, NULL);
        if (tb) { for (int i = 0; i < 4; i++) tb[i] = tab[i]; (*env)->ReleaseIntArrayElements(env, tableOut, tb, 0); }
    }
    if (srcRangeOut) {
        jint *sv = (*env)->GetIntArrayElements(env, srcRangeOut, NULL);
        if (sv) { sv[0] = srcRange; (*env)->ReleaseIntArrayElements(env, srcRangeOut, sv, 0); }
    }
    if (dstRangeOut) {
        jint *dv = (*env)->GetIntArrayElements(env, dstRangeOut, NULL);
        if (dv) { dv[0] = dstRange; (*env)->ReleaseIntArrayElements(env, dstRangeOut, dv, 0); }
    }
    if (brightnessOut) {
        jint *bv = (*env)->GetIntArrayElements(env, brightnessOut, NULL);
        if (bv) { bv[0] = brightness; (*env)->ReleaseIntArrayElements(env, brightnessOut, bv, 0); }
    }
    if (contrastOut) {
        jint *cv = (*env)->GetIntArrayElements(env, contrastOut, NULL);
        if (cv) { cv[0] = contrast; (*env)->ReleaseIntArrayElements(env, contrastOut, cv, 0); }
    }
    if (saturationOut) {
        jint *sv = (*env)->GetIntArrayElements(env, saturationOut, NULL);
        if (sv) { sv[0] = saturation; (*env)->ReleaseIntArrayElements(env, saturationOut, sv, 0); }
    }
    return 0;
}

/* palette conversion: palette holds 768 bytes of RGB (3x256) */
JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_swsConvertPalette8ToPacked32(JNIEnv *env, jobject thiz,
                                                         jbyteArray src, jint srcOff,
                                                         jbyteArray palette, jint palOff,
                                                         jbyteArray out, jint outOff, jint numPixels)
{
    if (!src || !out || !palette || numPixels <= 0)
        return;
    jbyte *s = (*env)->GetByteArrayElements(env, src, NULL);
    jbyte *o = (*env)->GetByteArrayElements(env, out, NULL);
    jbyte *p = (*env)->GetByteArrayElements(env, palette, NULL);
    if (s && o && p)
        sws_convertPalette8ToPacked32((const uint8_t *)(s + srcOff),
                                      (uint8_t *)(o + outOff),
                                      numPixels,
                                      (const uint8_t *)(p + palOff));
    if (s) (*env)->ReleaseByteArrayElements(env, src, s, JNI_ABORT);
    if (o) (*env)->ReleaseByteArrayElements(env, out, o, 0);
    if (p) (*env)->ReleaseByteArrayElements(env, palette, p, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_swsConvertPalette8ToPacked24(JNIEnv *env, jobject thiz,
                                                         jbyteArray src, jint srcOff,
                                                         jbyteArray palette, jint palOff,
                                                         jbyteArray out, jint outOff, jint numPixels)
{
    if (!src || !out || !palette || numPixels <= 0)
        return;
    jbyte *s = (*env)->GetByteArrayElements(env, src, NULL);
    jbyte *o = (*env)->GetByteArrayElements(env, out, NULL);
    jbyte *p = (*env)->GetByteArrayElements(env, palette, NULL);
    if (s && o && p)
        sws_convertPalette8ToPacked24((const uint8_t *)(s + srcOff),
                                      (uint8_t *)(o + outOff),
                                      numPixels,
                                      (const uint8_t *)(p + palOff));
    if (s) (*env)->ReleaseByteArrayElements(env, src, s, JNI_ABORT);
    if (o) (*env)->ReleaseByteArrayElements(env, out, o, 0);
    if (p) (*env)->ReleaseByteArrayElements(env, palette, p, JNI_ABORT);
}

/* ------------------------------------------------------------------ */
/* libswresample: alloc_set_opts2, is_initialized, get_class           */
/* ------------------------------------------------------------------ */

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_swrAllocSetOpts2(JNIEnv *env, jobject thiz,
                                              jint outFmt, jint outRate, jlong outMask,
                                              jint inFmt,  jint inRate,  jlong inMask)
{
    AVChannelLayout out_layout = {0};
    AVChannelLayout in_layout  = {0};
    av_channel_layout_from_mask(&out_layout, (uint64_t)outMask);
    av_channel_layout_from_mask(&in_layout,  (uint64_t)inMask);
    struct SwrContext *swr = NULL;
    int ret = swr_alloc_set_opts2(&swr, &out_layout, (enum AVSampleFormat)outFmt, outRate,
                                  &in_layout,  (enum AVSampleFormat)inFmt,  inRate,
                                  0, NULL);
    av_channel_layout_uninit(&out_layout);
    av_channel_layout_uninit(&in_layout);
    if (ret < 0 || !swr)
        return 0;
    return (jlong)(intptr_t)swr;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_swrIsInitialized(JNIEnv *env, jobject thiz, jlong ctx)
{
    struct SwrContext *c = PTR(struct SwrContext *, ctx);
    return c ? (jint)swr_is_initialized(c) : 0;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_swrGetClass(JNIEnv *env, jobject thiz)
{
    return (jlong)(intptr_t)swr_get_class();
}

/* ------------------------------------------------------------------ */
/* struct field access: stream metadata, codecpar, frame crops,        */
/* packet time_base                                                    */
/* ------------------------------------------------------------------ */

/* get a stream's metadata value by key without a box round-trip */
JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetMetadata(JNIEnv *env, jobject thiz,
                                               jlong ctx, jint index, jstring key)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c || index < 0 || index >= (jint)c->nb_streams || !key)
        return NULL;
    AVDictionary *d = c->streams[index]->metadata;
    if (!d)
        return NULL;
    const char *k = (*env)->GetStringUTFChars(env, key, NULL);
    AVDictionaryEntry *e = av_dict_get(d, k, NULL, 0);
    (*env)->ReleaseStringUTFChars(env, key, k);
    return e ? (*env)->NewStringUTF(env, e->value) : NULL;
}

/* set a stream's metadata value (streamSetMetadata) and free the key */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_streamSetMetadata(JNIEnv *env, jobject thiz,
                                               jlong ctx, jint index, jstring key, jstring val)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    if (!c || index < 0 || index >= (jint)c->nb_streams || !key)
        return AVERROR(EINVAL);
    const char *k = (*env)->GetStringUTFChars(env, key, NULL);
    const char *v = val ? (*env)->GetStringUTFChars(env, val, NULL) : NULL;
    int ret = av_dict_set(&c->streams[index]->metadata, k, v, 0);
    (*env)->ReleaseStringUTFChars(env, key, k);
    if (val)
        (*env)->ReleaseStringUTFChars(env, val, v);
    return ret;
}

/* AVCodecParameters remaining fields */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetParVideoDelay(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVCodecParameters *p = stream_par(PTR(AVFormatContext *, ctx), index);
    return p ? (jint)p->video_delay : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetParTrailingPadding(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVCodecParameters *p = stream_par(PTR(AVFormatContext *, ctx), index);
    return p ? (jint)p->trailing_padding : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetParSeekPreroll(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVCodecParameters *p = stream_par(PTR(AVFormatContext *, ctx), index);
    return p ? (jint)p->seek_preroll : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_streamGetParInitialPadding(JNIEnv *env, jobject thiz, jlong ctx, jint index)
{
    AVCodecParameters *p = stream_par(PTR(AVFormatContext *, ctx), index);
    return p ? (jint)p->initial_padding : 0;
}

/* AVFrame crop fields (decode-side display cropping) */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetCropTop(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    return f ? (jint)f->crop_top : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetCropBottom(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    return f ? (jint)f->crop_bottom : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetCropLeft(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    return f ? (jint)f->crop_left : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_frameGetCropRight(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    return f ? (jint)f->crop_right : 0;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_frameSetCrop(JNIEnv *env, jobject thiz, jlong frame,
                                          jint top, jint bottom, jint left, jint right)
{
    AVFrame *f = PTR(AVFrame *, frame);
    if (f) {
        f->crop_top    = (size_t)top;
        f->crop_bottom = (size_t)bottom;
        f->crop_left   = (size_t)left;
        f->crop_right  = (size_t)right;
    }
}

/* AVPacket.time_base (newer waveform-packet API field) */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_packetGetTimeBaseNum(JNIEnv *env, jobject thiz, jlong pkt)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    return p ? (jint)p->time_base.num : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_packetGetTimeBaseDen(JNIEnv *env, jobject thiz, jlong pkt)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    return p ? (jint)p->time_base.den : 0;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_packetSetTimeBase(JNIEnv *env, jobject thiz, jlong pkt,
                                               jint num, jint den)
{
    AVPacket *p = PTR(AVPacket *, pkt);
    if (p) { p->time_base.num = num; p->time_base.den = den; }
}

/* ------------------------------------------------------------------ */
/* libavutil: AVAudioFifo (sample-level audio buffers)                 */
/*                                                                     */
/* All data is exchanged as interleaved byte[] from Java; the C layer  */
/* bridges to the planar-plane model expected by libavutil using       */
/* av_samples_fill_arrays(). sampleFmt/channels must be supplied on    */
/* every write/read because AVAudioFifo is opaque.                     */
/*                                                                     */
/* The Java bridge is only meaningful for interleaved (packed) sample  */
/* formats; planar formats are rejected at allocation time.            */
/* ------------------------------------------------------------------ */

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_audioFifoAlloc(JNIEnv *env, jobject thiz,
                                            jint sampleFmt, jint channels, jint nbSamples)
{
    if (channels <= 0 || nbSamples < 0)
        return 0;
    /* The interleaved bridge only supports packed layouts. */
    if (av_sample_fmt_is_planar((enum AVSampleFormat)sampleFmt))
        return 0;
    AVAudioFifo *af = av_audio_fifo_alloc((enum AVSampleFormat)sampleFmt, channels, nbSamples);
    return (jlong)(intptr_t)af;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_audioFifoFree(JNIEnv *env, jobject thiz, jlong af)
{
    AVAudioFifo *f = PTR(AVAudioFifo *, af);
    if (f)
        av_audio_fifo_free(f);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_audioFifoWrite(JNIEnv *env, jobject thiz, jlong af,
                                            jint sampleFmt, jint channels,
                                            jbyteArray in, jint off, jint nbSamples)
{
    AVAudioFifo *f = PTR(AVAudioFifo *, af);
    if (!f || !in || channels <= 0 || nbSamples <= 0)
        return AVERROR(EINVAL);
    jbyte *buf = (*env)->GetByteArrayElements(env, in, NULL);
    if (!buf)
        return AVERROR(ENOMEM);
    int bps = av_get_bytes_per_sample((enum AVSampleFormat)sampleFmt);
    int planar = av_sample_fmt_is_planar((enum AVSampleFormat)sampleFmt);
    uint8_t *planes[AV_NUM_DATA_POINTERS] = {0};
    int linesize = 0;
    int ret = av_samples_fill_arrays(planes, &linesize,
                                     (const uint8_t *)(buf + off), channels,
                                     nbSamples, (enum AVSampleFormat)sampleFmt, 1);
    if (ret >= 0) {
        /* For planar formats each pointer already points into the interleaved
         * buffer as if it were plane-major; this matches how we later read. */
        ret = av_audio_fifo_write(f, (void * const *)planes, nbSamples);
        /* docs: guaranteed == nbSamples on success */
        if (ret < 0 || ret != nbSamples)
            ret = ret < 0 ? ret : AVERROR(EINVAL);
    }
    (*env)->ReleaseByteArrayElements(env, in, buf, JNI_ABORT);
    return ret;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_audioFifoRead(JNIEnv *env, jobject thiz, jlong af,
                                           jint sampleFmt, jint channels,
                                           jbyteArray out, jint off, jint maxSamples)
{
    AVAudioFifo *f = PTR(AVAudioFifo *, af);
    if (!f || !out || channels <= 0)
        return AVERROR(EINVAL);
    int avail = av_audio_fifo_size(f);
    int want = (maxSamples > 0 && maxSamples < avail) ? maxSamples : avail;
    if (want <= 0)
        return 0;
    jbyte *buf = (*env)->GetByteArrayElements(env, out, NULL);
    if (!buf)
        return AVERROR(ENOMEM);
    uint8_t *planes[AV_NUM_DATA_POINTERS] = {0};
    int linesize = 0;
    int ret = av_samples_fill_arrays(planes, &linesize,
                                     (const uint8_t *)(buf + off), channels,
                                     want, (enum AVSampleFormat)sampleFmt, 1);
    if (ret >= 0)
        ret = av_audio_fifo_read(f, (void * const *)planes, want);
    (*env)->ReleaseByteArrayElements(env, out, buf, 0);
    return ret < 0 ? ret : want;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_audioFifoPeek(JNIEnv *env, jobject thiz, jlong af,
                                           jint sampleFmt, jint channels,
                                           jbyteArray out, jint off, jint maxSamples)
{
    AVAudioFifo *f = PTR(AVAudioFifo *, af);
    if (!f || !out || channels <= 0)
        return AVERROR(EINVAL);
    int avail = av_audio_fifo_size(f);
    int want = (maxSamples > 0 && maxSamples < avail) ? maxSamples : avail;
    if (want <= 0)
        return 0;
    jbyte *buf = (*env)->GetByteArrayElements(env, out, NULL);
    if (!buf)
        return AVERROR(ENOMEM);
    uint8_t *planes[AV_NUM_DATA_POINTERS] = {0};
    int linesize = 0;
    int ret = av_samples_fill_arrays(planes, &linesize,
                                     (const uint8_t *)(buf + off), channels,
                                     want, (enum AVSampleFormat)sampleFmt, 1);
    if (ret >= 0)
        ret = av_audio_fifo_peek(f, (void * const *)planes, want);
    (*env)->ReleaseByteArrayElements(env, out, buf, 0);
    return ret < 0 ? ret : want;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_audioFifoDrain(JNIEnv *env, jobject thiz, jlong af, jint nbSamples)
{
    AVAudioFifo *f = PTR(AVAudioFifo *, af);
    return f ? (jint)av_audio_fifo_drain(f, nbSamples) : AVERROR(EINVAL);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_audioFifoSize(JNIEnv *env, jobject thiz, jlong af)
{
    AVAudioFifo *f = PTR(AVAudioFifo *, af);
    return f ? (jint)av_audio_fifo_size(f) : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_audioFifoSpace(JNIEnv *env, jobject thiz, jlong af)
{
    AVAudioFifo *f = PTR(AVAudioFifo *, af);
    return f ? (jint)av_audio_fifo_space(f) : 0;
}

/* ================================================================== */
/* Round 5: remaining public-API gap closure                            */
/*                                                                     */
/* Adds the last uncovered FFmpeg public entry points: input-format    */
/* selection when opening input, subtitle decoding apparatus, codec    */
/* descriptor / hw-config / class queries, timestamp math, display     */
/* (rotation) helpers, filter graph parse/parse2 and swscale class     */
/* allocation.                                                         */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* libavformat: open input with an explicit AVInputFormat* handle     */
/* ------------------------------------------------------------------ */

/* Variant of formatOpenInput that honours the AVInputFormat handle
 * returned by findInputFormat(). Matches avformat_open_input()'s third
 * parameter. */
JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_formatOpenInputFmt(JNIEnv *env, jobject thiz,
                                                jlong inputFmt, jstring url, jlong optionsBox)
{
    if (!url)
        return 0;
    const AVInputFormat *ifmt = inputFmt ? PTR(const AVInputFormat *, inputFmt) : NULL;
    AVDictionary **opts = optionsBox ? PTR(AVDictionary **, optionsBox) : NULL;
    AVFormatContext *ctx = NULL;
    const char *u = (*env)->GetStringUTFChars(env, url, NULL);
    int ret = avformat_open_input(&ctx, u, ifmt, opts);
    (*env)->ReleaseStringUTFChars(env, url, u);
    if (ret < 0 || !ctx)
        return 0;
    return (jlong)(intptr_t)ctx;
}

/* ------------------------------------------------------------------ */
/* libavcodec: subtitle decoding                                       */
/* ------------------------------------------------------------------ */

/* Allocate a zero-initialised (heap) AVSubtitle. Free the produced data
 * with subtitleFree() / avsubtitle_free()). The object holds no aliased
 * buffers on its own so it can be stack-free after avsubtitle_free(), but
 * we still allocate it so Java can keep a stable long handle. */
JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_subtitleAlloc(JNIEnv *env, jobject thiz)
{
    AVSubtitle *sub = (AVSubtitle *)av_mallocz(sizeof(AVSubtitle));
    return (jlong)(intptr_t)sub;
}

/* Free decoded subtitle data (and the wrapper object). */
JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_subtitleFree(JNIEnv *env, jobject thiz, jlong sub)
{
    AVSubtitle *s = PTR(AVSubtitle *, sub);
    if (s) {
        avsubtitle_free(s);
        av_freep(&s);
    }
}

/* Decode one subtitle packet into a preallocated AVSubtitle.
 * @param gotSubPtr a 1-element int[] receiving got_sub_ptr.
 * @return 0 on success (>= 0), negative AVERROR otherwise. When gotSubPtr[0]
 *         is nonzero the caller must call subtitleFree() on `sub`. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_subtitleDecode(JNIEnv *env, jobject thiz,
                                            jlong codecCtx, jlong sub, jlong pkt,
                                            jintArray gotSubPtr)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    AVSubtitle *s = PTR(AVSubtitle *, sub);
    AVPacket *p = pkt ? PTR(AVPacket *, pkt) : NULL;
    if (!cc || !s)
        return AVERROR(EINVAL);
    int got = 0;
    int ret = avcodec_decode_subtitle2(cc, s, &got, p);
    if (gotSubPtr) {
        jint *g = (*env)->GetIntArrayElements(env, gotSubPtr, NULL);
        if (g) {
            g[0] = got;
            (*env)->ReleaseIntArrayElements(env, gotSubPtr, g, 0);
        }
    }
    return ret;
}

/* AVSubtitle field getters */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_subtitleGetNumRects(JNIEnv *env, jobject thiz, jlong sub)
{
    AVSubtitle *s = PTR(AVSubtitle *, sub);
    return s ? (jint)s->num_rects : 0;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_subtitleGetPts(JNIEnv *env, jobject thiz, jlong sub)
{
    AVSubtitle *s = PTR(AVSubtitle *, sub);
    return s ? (jlong)s->pts : AV_NOPTS_VALUE;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_subtitleGetFormat(JNIEnv *env, jobject thiz, jlong sub)
{
    AVSubtitle *s = PTR(AVSubtitle *, sub);
    return s ? (jint)s->format : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_subtitleGetStartDisplayTime(JNIEnv *env, jobject thiz, jlong sub)
{
    AVSubtitle *s = PTR(AVSubtitle *, sub);
    return s ? (jint)s->start_display_time : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_subtitleGetEndDisplayTime(JNIEnv *env, jobject thiz, jlong sub)
{
    AVSubtitle *s = PTR(AVSubtitle *, sub);
    return s ? (jint)s->end_display_time : 0;
}

/* Get the plain-text or ASS text of a subtitle rectangle.
 * @param rectIdx index into sub->rects.
 * @param wantAss 1 for the ASS/SSA text, 0 for the plain text. */
JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_subtitleRectGetText(JNIEnv *env, jobject thiz,
                                                 jlong sub, jint rectIdx, jint wantAss)
{
    AVSubtitle *s = PTR(AVSubtitle *, sub);
    if (!s || rectIdx < 0 || (unsigned)rectIdx >= s->num_rects || !s->rects[rectIdx])
        return NULL;
    AVSubtitleRect *r = s->rects[rectIdx];
    const char *t = wantAss ? r->ass : r->text;
    return t ? (*env)->NewStringUTF(env, t) : NULL;
}

/* Geometry / type of a subtitle rectangle. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_subtitleRectGetX(JNIEnv *env, jobject thiz, jlong sub, jint rectIdx)
{
    AVSubtitle *s = PTR(AVSubtitle *, sub);
    if (!s || rectIdx < 0 || (unsigned)rectIdx >= s->num_rects || !s->rects[rectIdx])
        return 0;
    return (jint)s->rects[rectIdx]->x;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_subtitleRectGetY(JNIEnv *env, jobject thiz, jlong sub, jint rectIdx)
{
    AVSubtitle *s = PTR(AVSubtitle *, sub);
    if (!s || rectIdx < 0 || (unsigned)rectIdx >= s->num_rects || !s->rects[rectIdx])
        return 0;
    return (jint)s->rects[rectIdx]->y;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_subtitleRectGetW(JNIEnv *env, jobject thiz, jlong sub, jint rectIdx)
{
    AVSubtitle *s = PTR(AVSubtitle *, sub);
    if (!s || rectIdx < 0 || (unsigned)rectIdx >= s->num_rects || !s->rects[rectIdx])
        return 0;
    return (jint)s->rects[rectIdx]->w;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_subtitleRectGetH(JNIEnv *env, jobject thiz, jlong sub, jint rectIdx)
{
    AVSubtitle *s = PTR(AVSubtitle *, sub);
    if (!s || rectIdx < 0 || (unsigned)rectIdx >= s->num_rects || !s->rects[rectIdx])
        return 0;
    return (jint)s->rects[rectIdx]->h;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_subtitleRectGetType(JNIEnv *env, jobject thiz, jlong sub, jint rectIdx)
{
    AVSubtitle *s = PTR(AVSubtitle *, sub);
    if (!s || rectIdx < 0 || (unsigned)rectIdx >= s->num_rects || !s->rects[rectIdx])
        return -1;
    return (jint)s->rects[rectIdx]->type;
}

/* ------------------------------------------------------------------ */
/* libass: application-driven subtitle rendering                       */
/*                                                                     */
/* The built-in "subtitles"/"ass" filters RE-OPEN the media file with  */
/* their own demuxer at graph-config time (vf_subtitles's               */
/* init_subtitles reads the whole input once and re-derives every      */
/* subtitle event). When an app already owns the source via the        */
/* format* APIs above that is duplicate IO (a full pass over the file, */
/* a separate http/https connection) and it ignores the opened        */
/* context's options and interrupt_callback. These thin libass         */
/* wrappers let such an app feed the subtitle events IT demuxed/       */
/* decoded to libass directly (track per text subtitle stream),        */
/* register MKV-embedded fonts (AVMEDIA_TYPE_ATTACHMENT streams;        */
/* font bytes live in codecpar->extradata, name/mimetype in stream     */
/* metadata) with assAddFont, and render the ASS Image list itself.    */
/*                                                                     */
/* Compiled only when FFmpeg was configured with --enable-libass; the  */
/* Java class always declares the methods, so on a non-libass build    */
/* the first CALL raises UnsatisfiedLinkError. Handles follow the      */
/* usual long convention (0 == NULL). Rendered ASS_Image bitmaps are   */
/* 8-bit alpha strips, stride bytes wide, valid until the next         */
/* assRenderFrame call on the same renderer.                          */
/* ------------------------------------------------------------------ */

/* Size of AVCodecContext::subtitle_header (SSA style/format header). */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_codecGetSubtitleHeaderSize(JNIEnv *env, jobject thiz, jlong codecCtx)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    return (cc && cc->subtitle_header && cc->subtitle_header_size > 0)
           ? cc->subtitle_header_size : 0;
}

/* Copy up to len bytes of the decoder's subtitle_header into out. For SSA/ASS
 * this is the script format header ([Script Info]/[V4 Styles]); pass it to
 * assProcessCodecPrivate() once after opening the decoder.
 * @return bytes copied, or -1 when there is no subtitle_header. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_codecGetSubtitleHeader(JNIEnv *env, jobject thiz,
                                                    jlong codecCtx, jbyteArray out,
                                                    jint off, jint len)
{
    AVCodecContext *cc = PTR(AVCodecContext *, codecCtx);
    if (!cc || !out || off < 0 || len <= 0 ||
        (jsize)off + (jsize)len > (*env)->GetArrayLength(env, out))
        return AVERROR(EINVAL);
    if (!cc->subtitle_header || cc->subtitle_header_size <= 0)
        return -1;
    jint n = (cc->subtitle_header_size < len) ? cc->subtitle_header_size : len;
    (*env)->SetByteArrayRegion(env, out, off, n, (const jbyte *)cc->subtitle_header);
    return n;
}

#if CONFIG_LIBASS

/* ASS_Library lifecycle (ass_library_init/done). One library can back many
 * renderers/tracks; the embedded-font cache used by assAddFont is global to
 * the process but is managed through a library handle. */
JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_assLibCreate(JNIEnv *env, jobject thiz)
{
    return (jlong)(intptr_t)ass_library_init();
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_assLibFree(JNIEnv *env, jobject thiz, jlong lib)
{
    ASS_Library *l = PTR(ASS_Library *, lib);
    if (l)
        ass_library_done(l);
}

/* ASS_Renderer: owns rasterization state (frame size, margins, font select). */
JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_assRendererCreate(JNIEnv *env, jobject thiz, jlong lib)
{
    ASS_Library *l = PTR(ASS_Library *, lib);
    return l ? (jlong)(intptr_t)ass_renderer_init(l) : 0;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_assRendererFree(JNIEnv *env, jobject thiz, jlong rend)
{
    ASS_Renderer *r = PTR(ASS_Renderer *, rend);
    if (r)
        ass_renderer_done(r);
}

/* ASS_Track: parsed script state (styles + events) for ONE subtitle stream. */
JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_assTrackCreate(JNIEnv *env, jobject thiz, jlong lib)
{
    ASS_Library *l = PTR(ASS_Library *, lib);
    return l ? (jlong)(intptr_t)ass_new_track(l) : 0;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_assTrackFree(JNIEnv *env, jobject thiz, jlong track)
{
    ASS_Track *t = PTR(ASS_Track *, track);
    if (t)
        ass_free_track(t);
}

/* Drop all currently queued (visible) events; call on seek. */
JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_assTrackFlushEvents(JNIEnv *env, jobject thiz, jlong track)
{
    ASS_Track *t = PTR(ASS_Track *, track);
    if (t)
        ass_flush_events(t);
}

/* Script's PlayResX/PlayResY (0 when the script omitted them; then libass
 * uses its default and callers usually scale by the frame size regardless). */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_assTrackGetPlayResX(JNIEnv *env, jobject thiz, jlong track)
{
    ASS_Track *t = PTR(ASS_Track *, track);
    return t ? (jint)t->PlayResX : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_assTrackGetPlayResY(JNIEnv *env, jobject thiz, jlong track)
{
    ASS_Track *t = PTR(ASS_Track *, track);
    return t ? (jint)t->PlayResY : 0;
}

/* Register an in-memory font (e.g. an MKV AttachedFile extracted through
 * streamGetCodecType == AVMEDIA_TYPE_ATTACHMENT + streamGetMetadata +
 * streamGetExtradata). data is copied; name should keep the stored
 * attachment filename (extension included) so the subtitle script's family
 * references resolve. assClearFonts(lib) drops the fonts registered with that
 * library. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_assAddFont(JNIEnv *env, jobject thiz, jlong lib,
                                        jstring name, jbyteArray in, jint off, jint len)
{
    ASS_Library *l = PTR(ASS_Library *, lib);
    if (!l || !name || !in || off < 0 || len <= 0 ||
        (jsize)off + (jsize)len > (*env)->GetArrayLength(env, in))
        return AVERROR(EINVAL);
    const char *n = (*env)->GetStringUTFChars(env, name, NULL);
    if (!n)
        return AVERROR(ENOMEM);
    uint8_t *buf = (uint8_t *)av_malloc(len);
    if (!buf) {
        (*env)->ReleaseStringUTFChars(env, name, n);
        return AVERROR(ENOMEM);
    }
    (*env)->GetByteArrayRegion(env, in, off, len, (jbyte *)buf);
    ass_add_font(l, n, (const char *)buf, len);
    (*env)->ReleaseStringUTFChars(env, name, n);
    av_free(buf);
    return 0;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_assClearFonts(JNIEnv *env, jobject thiz, jlong lib)
{
    ASS_Library *l = PTR(ASS_Library *, lib);
    if (l)
        ass_clear_fonts(l);
}

/* Directory scanned for extra fonts by the selected font provider. */
JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_assSetFontsDir(JNIEnv *env, jobject thiz, jlong lib, jstring dir)
{
    ASS_Library *l = PTR(ASS_Library *, lib);
    if (!l || !dir)
        return;
    const char *d = (*env)->GetStringUTFChars(env, dir, NULL);
    ass_set_fonts_dir(l, d);
    (*env)->ReleaseStringUTFChars(env, dir, d);
}

/* ass_set_fonts passthrough: provider = ASS_FONT_SELECT_* (Java constants:
 * OFF=0, ENABLE=1, INVERT=2). defaultFont is an optional .ttf file (fallback
 * family last resort); defaultFamily names the built-in default style font.
 * @return 0 on success, negative AVERROR if the renderer was null. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_assSetFonts(JNIEnv *env, jobject thiz, jlong rend,
                                         jstring defaultFont, jstring defaultFamily,
                                         jint provider)
{
    ASS_Renderer *r = PTR(ASS_Renderer *, rend);
    if (!r)
        return AVERROR(EINVAL);
    const char *df = defaultFont ? (*env)->GetStringUTFChars(env, defaultFont, NULL) : NULL;
    const char *fam = defaultFamily ? (*env)->GetStringUTFChars(env, defaultFamily, NULL) : NULL;
    ass_set_fonts(r, df, fam, (int)provider, NULL, 0);
    if (defaultFont)
        (*env)->ReleaseStringUTFChars(env, defaultFont, df);
    if (defaultFamily)
        (*env)->ReleaseStringUTFChars(env, defaultFamily, fam);
    return 0;
}

/* Renderer geometry / style tweaks. */
JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_assSetFrameSize(JNIEnv *env, jobject thiz, jlong rend, jint w, jint h)
{
    ASS_Renderer *r = PTR(ASS_Renderer *, rend);
    if (r)
        ass_set_frame_size(r, w, h);
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_assSetStorageSize(JNIEnv *env, jobject thiz, jlong rend, jint w, jint h)
{
    ASS_Renderer *r = PTR(ASS_Renderer *, rend);
    if (r)
        ass_set_storage_size(r, w, h);
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_assSetMargins(JNIEnv *env, jobject thiz, jlong rend,
                                           jint top, jint bottom, jint left, jint right)
{
    ASS_Renderer *r = PTR(ASS_Renderer *, rend);
    if (r)
        ass_set_margins(r, top, bottom, left, right);
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_assSetUseMargins(JNIEnv *env, jobject thiz, jlong rend, jint use)
{
    ASS_Renderer *r = PTR(ASS_Renderer *, rend);
    if (r)
        ass_set_use_margins(r, use);
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_assSetFontScale(JNIEnv *env, jobject thiz, jlong rend, jdouble scale)
{
    ASS_Renderer *r = PTR(ASS_Renderer *, rend);
    if (r)
        ass_set_font_scale(r, scale);
}

/* The three "feed the track" entry points. libass may modify the input
 * buffer while parsing, so all of them pass a private copy. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_assProcessCodecPrivate(JNIEnv *env, jobject thiz, jlong track,
                                                    jbyteArray in, jint off, jint len)
{
    ASS_Track *t = PTR(ASS_Track *, track);
    if (!t || !in || off < 0 || len <= 0 ||
        (jsize)off + (jsize)len > (*env)->GetArrayLength(env, in))
        return AVERROR(EINVAL);
    char *buf = (char *)av_malloc(len);
    if (!buf)
        return AVERROR(ENOMEM);
    (*env)->GetByteArrayRegion(env, in, off, len, (jbyte *)buf);
    ass_process_codec_private(t, buf, len);
    av_free(buf);
    return 0;
}

/* Feed raw script/event text (e.g. one MKV ASS packet = one "Dialogue:"
 * line). Incremental: libass buffers until an event is complete. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_assProcessData(JNIEnv *env, jobject thiz, jlong track,
                                            jbyteArray in, jint off, jint len)
{
    ASS_Track *t = PTR(ASS_Track *, track);
    if (!t || !in || off < 0 || len <= 0 ||
        (jsize)off + (jsize)len > (*env)->GetArrayLength(env, in))
        return AVERROR(EINVAL);
    char *buf = (char *)av_malloc(len + 1);
    if (!buf)
        return AVERROR(ENOMEM);
    (*env)->GetByteArrayRegion(env, in, off, len, (jbyte *)buf);
    buf[len] = '\0';
    ass_process_data(t, buf, len);
    av_free(buf);
    return 0;
}

/* Feed ONE already-timed event (decoded SRT/WebVTS text): start/duration in
 * milliseconds (use subtitleGetPts * 1000 / AV_TIME_BASE plus
 * subtitleGetStart/EndDisplayTime). */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_assProcessChunk(JNIEnv *env, jobject thiz, jlong track,
                                             jbyteArray in, jint off, jint len,
                                             jlong startMs, jlong durationMs)
{
    ASS_Track *t = PTR(ASS_Track *, track);
    if (!t || !in || off < 0 || len <= 0 ||
        (jsize)off + (jsize)len > (*env)->GetArrayLength(env, in))
        return AVERROR(EINVAL);
    char *buf = (char *)av_malloc(len + 1);
    if (!buf)
        return AVERROR(ENOMEM);
    (*env)->GetByteArrayRegion(env, in, off, len, (jbyte *)buf);
    buf[len] = '\0';
    ass_process_chunk(t, buf, len, (long long)startMs, (long long)durationMs);
    av_free(buf);
    return 0;
}

/* Rasterize track at nowMs (video clock). detectChange (optional 1-element
 * int[]) receives libass's ASS_CHANGE_* bitmask. @return the first ASS_Image
 * handle (walk with assImageGetNext*), 0 when nothing is visible. The list
 * is owned by the renderer and re-generated by every call here. */
JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_assRenderFrame(JNIEnv *env, jobject thiz, jlong rend,
                                            jlong track, jlong nowMs, jintArray detectChange)
{
    ASS_Renderer *r = PTR(ASS_Renderer *, rend);
    ASS_Track *t = PTR(ASS_Track *, track);
    if (!r || !t)
        return 0;
    int dc = 0;
    ASS_Image *img = ass_render_frame(r, t, (long long)nowMs, &dc);
    if (detectChange) {
        jint v = dc;
        (*env)->SetIntArrayRegion(env, detectChange, 0, 1, &v);
    }
    return (jlong)(intptr_t)img;
}

/* ASS_Image list accessors (0 / -1 on a null handle). */
JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_assImageGetNext(JNIEnv *env, jobject thiz, jlong img)
{
    ASS_Image *i = PTR(ASS_Image *, img);
    return (i && i->next) ? (jlong)(intptr_t)i->next : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_assImageGetWidth(JNIEnv *env, jobject thiz, jlong img)
{
    ASS_Image *i = PTR(ASS_Image *, img);
    return i ? (jint)i->w : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_assImageGetHeight(JNIEnv *env, jobject thiz, jlong img)
{
    ASS_Image *i = PTR(ASS_Image *, img);
    return i ? (jint)i->h : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_assImageGetStride(JNIEnv *env, jobject thiz, jlong img)
{
    ASS_Image *i = PTR(ASS_Image *, img);
    return i ? (jint)i->stride : 0;
}

/* Foreground color as a packed unsigned ARGB value. */
JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_assImageGetColor(JNIEnv *env, jobject thiz, jlong img)
{
    ASS_Image *i = PTR(ASS_Image *, img);
    return i ? (jlong)(unsigned int)i->color : 0;
}

/* Image layer type (0 character, 1 outline, 2 shadow). */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_assImageGetType(JNIEnv *env, jobject thiz, jlong img)
{
    ASS_Image *i = PTR(ASS_Image *, img);
    return i ? (jint)i->type : 0;
}

/* Top-left screen position of this glyph run. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_assImageGetTop(JNIEnv *env, jobject thiz, jlong img)
{
    ASS_Image *i = PTR(ASS_Image *, img);
    return i ? (jint)i->dst_y : 0;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_assImageGetLeft(JNIEnv *env, jobject thiz, jlong img)
{
    ASS_Image *i = PTR(ASS_Image *, img);
    return i ? (jint)i->dst_x : 0;
}

/* Copy the alpha bitmap (stride*h bytes) into out; out must be big enough.
 * @return bytes copied, negative AVERROR on bad args / small buffer. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_assImageGetBitmap(JNIEnv *env, jobject thiz, jlong img,
                                               jbyteArray out, jint off, jint len)
{
    ASS_Image *i = PTR(ASS_Image *, img);
    if (!i || !i->bitmap || !out || off < 0 || len <= 0 ||
        (jsize)off + (jsize)len > (*env)->GetArrayLength(env, out))
        return AVERROR(EINVAL);
    if (i->stride <= 0 || i->h <= 0)
        return 0;
    int needed = i->stride * i->h;
    if (len < needed)
        return AVERROR(ENOMEM);
    (*env)->SetByteArrayRegion(env, out, off, needed, (const jbyte *)i->bitmap);
    return needed;
}

#endif /* CONFIG_LIBASS */

/* ------------------------------------------------------------------ */
/* libavcodec: descriptor iteration / hw config / class                */
/* ------------------------------------------------------------------ */

/* Iterate codec descriptors. Pass 0 to start (returns the first descriptor),
 * then pass the previous descriptor handle to advance. Returns 0 at end. */
JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_codecDescriptorNext(JNIEnv *env, jobject thiz, jlong prev)
{
    const AVCodecDescriptor *p = PTR(const AVCodecDescriptor *, prev);
    return (jlong)(intptr_t)avcodec_descriptor_next(p);
}

/* Find a descriptor by codec id (complement to codecDescriptorNext). */
JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_codecDescriptorGet(JNIEnv *env, jobject thiz, jint codecId)
{
    return (jlong)(intptr_t)avcodec_descriptor_get((enum AVCodecID)codecId);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_codecDescriptorGetId(JNIEnv *env, jobject thiz, jlong desc)
{
    const AVCodecDescriptor *d = PTR(const AVCodecDescriptor *, desc);
    return d ? (jint)d->id : (jint)AV_CODEC_ID_NONE;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_codecDescriptorGetType(JNIEnv *env, jobject thiz, jlong desc)
{
    const AVCodecDescriptor *d = PTR(const AVCodecDescriptor *, desc);
    return d ? (jint)d->type : (jint)AVMEDIA_TYPE_UNKNOWN;
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_codecDescriptorGetName(JNIEnv *env, jobject thiz, jlong desc)
{
    const AVCodecDescriptor *d = PTR(const AVCodecDescriptor *, desc);
    return (d && d->name) ? (*env)->NewStringUTF(env, d->name) : NULL;
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_codecDescriptorGetLongName(JNIEnv *env, jobject thiz, jlong desc)
{
    const AVCodecDescriptor *d = PTR(const AVCodecDescriptor *, desc);
    return (d && d->long_name) ? (*env)->NewStringUTF(env, d->long_name) : NULL;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_codecDescriptorGetProps(JNIEnv *env, jobject thiz, jlong desc)
{
    const AVCodecDescriptor *d = PTR(const AVCodecDescriptor *, desc);
    return d ? (jint)d->props : 0;
}

/* Query a codec's HDMI hardware configuration. Returns a 3-element int[]
 * {pix_fmt, methods, device_type}, or null when the index is out of range
 * or the codec has no hardware support. */
JNIEXPORT jintArray JNICALL
Java_org_ffmpeg_FFMpegNative_codecGetHwConfig(JNIEnv *env, jobject thiz, jlong codec, jint index)
{
    const AVCodec *c = PTR(const AVCodec *, codec);
    if (!c)
        return NULL;
    const AVCodecHWConfig *hw = avcodec_get_hw_config(c, index);
    if (!hw)
        return NULL;
    jintArray arr = (*env)->NewIntArray(env, 3);
    if (!arr)
        return NULL;
    jint vals[3];
    vals[0] = (jint)hw->pix_fmt;
    vals[1] = (jint)hw->methods;
    vals[2] = (jint)hw->device_type;
    (*env)->SetIntArrayRegion(env, arr, 0, 3, vals);
    return arr;
}

/* @return AVClass handle of libavcodec (for option introspection). */
JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_codecGetClass(JNIEnv *env, jobject thiz)
{
    return (jlong)(intptr_t)avcodec_get_class();
}

/* ------------------------------------------------------------------ */
/* libavutil: timestamp math (av_compare_mod / av_add_stable)         */
/* ------------------------------------------------------------------ */

/* Compare a and b in the modulo domain (e.g. wrap-around RTP/NTP). */
JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_compareMod(JNIEnv *env, jobject thiz, jlong a, jlong b, jlong mod)
{
    return (jlong)av_compare_mod((uint64_t)a, (uint64_t)b, (uint64_t)mod);
}

/* Add `inc` (in inc_tb units) to `ts` (in ts_tb units), avoiding precision
 * drift. @return the new timestamp in ts_tb units. */
JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_addStable(JNIEnv *env, jobject thiz,
                                       jint tsTbNum, jint tsTbDen, jlong ts,
                                       jint incTbNum, jint incTbDen, jlong inc)
{
    AVRational ts_tb = { tsTbNum, tsTbDen };
    AVRational inc_tb = { incTbNum, incTbDen };
    return (jlong)av_add_stable(ts_tb, (int64_t)ts, inc_tb, (int64_t)inc);
}

/* ------------------------------------------------------------------ */
/* libavutil: display (rotation) helpers                               */
/*                                                                     */
/* The sidebar matrix is a 3x3 row-major int32 array (9 ints). We       */
/* exchange it as a Java int[] of length 9 through a static native      */
/* scratch as JNI cannot otherwise pass a raw int32_t[9] by value.      */
/* ------------------------------------------------------------------ */

/* Extract the rotation angle (degrees) from a display matrix. */
JNIEXPORT jdouble JNICALL
Java_org_ffmpeg_FFMpegNative_displayRotationGet(JNIEnv *env, jobject thiz, jintArray matrix)
{
    if (!matrix)
        return 0.0;
    jsize n = (*env)->GetArrayLength(env, matrix);
    if (n < 9)
        return 0.0;
    jint m[9];
    (*env)->GetIntArrayRegion(env, matrix, 0, 9, m);
    int32_t mat[9];
    for (int i = 0; i < 9; i++)
        mat[i] = (int32_t)m[i];
    return (jdouble)av_display_rotation_get(mat);
}

/* Set the rotation angle in a display matrix (in place). */
JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_displayRotationSet(JNIEnv *env, jobject thiz, jintArray matrix, jdouble angle)
{
    if (!matrix)
        return;
    jsize n = (*env)->GetArrayLength(env, matrix);
    if (n < 9)
        return;
    jint m[9];
    (*env)->GetIntArrayRegion(env, matrix, 0, 9, m);
    int32_t mat[9];
    for (int i = 0; i < 9; i++)
        mat[i] = (int32_t)m[i];
    av_display_rotation_set(mat, (double)angle);
    for (int i = 0; i < 9; i++)
        m[i] = (jint)mat[i];
    (*env)->SetIntArrayRegion(env, matrix, 0, 9, m);
}

/* Flip a display matrix along the horizontal and/or vertical axis. */
JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_displayMatrixFlip(JNIEnv *env, jobject thiz, jintArray matrix,
                                               jint hflip, jint vflip)
{
    if (!matrix)
        return;
    jsize n = (*env)->GetArrayLength(env, matrix);
    if (n < 9)
        return;
    jint m[9];
    (*env)->GetIntArrayRegion(env, matrix, 0, 9, m);
    int32_t mat[9];
    for (int i = 0; i < 9; i++)
        mat[i] = (int32_t)m[i];
    av_display_matrix_flip(mat, hflip, vflip);
    for (int i = 0; i < 9; i++)
        m[i] = (jint)mat[i];
    (*env)->SetIntArrayRegion(env, matrix, 0, 9, m);
}

/* ------------------------------------------------------------------ */
/* libavfilter: graph_parse / graph_parse2                             */
/* ------------------------------------------------------------------ */

/* Convenience: allocate a single AVFilterInOut head. */
static AVFilterInOut *avfio_alloc(void)
{
    AVFilterInOut *io = avfilter_inout_alloc();
    return io;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_filterInOutAlloc(JNIEnv *env, jobject thiz)
{
    return (jlong)(intptr_t)avfio_alloc();
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_filterInOutFree(JNIEnv *env, jobject thiz, jlong io)
{
    AVFilterInOut *p = PTR(AVFilterInOut *, io);
    if (p)
        avfilter_inout_free(&p);
}

/* Set the label / link target of an AVFilterInOut. Called by Java when
 * wiring parse results. */
JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_filterInOutSetName(JNIEnv *env, jobject thiz, jlong io, jstring name)
{
    AVFilterInOut *p = PTR(AVFilterInOut *, io);
    if (!p)
        return;
    const char *n = name ? (*env)->GetStringUTFChars(env, name, NULL) : NULL;
    av_freep(&p->name);
    p->name = n ? av_strdup(n) : NULL;
    if (name) (*env)->ReleaseStringUTFChars(env, name, n);
}

JNIEXPORT jstring JNICALL
Java_org_ffmpeg_FFMpegNative_filterInOutGetName(JNIEnv *env, jobject thiz, jlong io)
{
    const AVFilterInOut *p = PTR(const AVFilterInOut *, io);
    return (p && p->name) ? (*env)->NewStringUTF(env, p->name) : NULL;
}

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_filterInOutGetNext(JNIEnv *env, jobject thiz, jlong io)
{
    const AVFilterInOut *p = PTR(const AVFilterInOut *, io);
    return (jlong)(intptr_t)(p ? p->next : NULL);
}

/* avfilter_graph_parse: parse a filter chain, linking two explicitly
 * allocated AVFilterInOut lists (as accepted by avfilter_graph_parse_ptr
 * style callers) into the graph. `graph` owns the inout lists afterwards. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_filterGraphParse(JNIEnv *env, jobject thiz,
                                              jlong graph, jstring filters,
                                              jlong inputs, jlong outputs)
{
    AVFilterGraph *g = PTR(AVFilterGraph *, graph);
    AVFilterInOut *in = inputs  ? PTR(AVFilterInOut *, inputs)  : NULL;
    AVFilterInOut *out = outputs ? PTR(AVFilterInOut *, outputs) : NULL;
    if (!g || !filters)
        return AVERROR(EINVAL);
    const char *f = (*env)->GetStringUTFChars(env, filters, NULL);
    int ret = avfilter_graph_parse(g, f, in, out, NULL);
    (*env)->ReleaseStringUTFChars(env, filters, f);
    return ret;
}

/* avfilter_graph_parse2: parse a filter chain describing its own [labels].
 * Returns inputs/outputs as linked AVFilterInOut lists (Java must free each
 * with filterInOutFree on the head; the `next` chain is followed via
 * filterInOutGetNext). */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_filterGraphParse2(JNIEnv *env, jobject thiz,
                                               jlong graph, jstring filters,
                                               jlongArray inputsOut, jlongArray outputsOut)
{
    AVFilterGraph *g = PTR(AVFilterGraph *, graph);
    if (!g || !filters)
        return AVERROR(EINVAL);
    const char *f = (*env)->GetStringUTFChars(env, filters, NULL);
    AVFilterInOut *inputs = NULL;
    AVFilterInOut *outputs = NULL;
    int ret = avfilter_graph_parse2(g, f, &inputs, &outputs);
    (*env)->ReleaseStringUTFChars(env, filters, f);
    if (inputsOut && inputs) {
        jlong v[1] = { (jlong)(intptr_t)inputs };
        (*env)->SetLongArrayRegion(env, inputsOut, 0, 1, v);
    }
    if (outputsOut && outputs) {
        jlong v[1] = { (jlong)(intptr_t)outputs };
        (*env)->SetLongArrayRegion(env, outputsOut, 0, 1, v);
    }
    return ret;
}

/* ------------------------------------------------------------------ */
/* libswscale: class / manual alloc                                    */
/* ------------------------------------------------------------------ */

/* @return AVClass handle of libswscale. */
JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_swsGetClass(JNIEnv *env, jobject thiz)
{
    return (jlong)(intptr_t)sws_get_class();
}

/* Allocate an options-enabled SwsContext (configure via optSet*, then call
 * swsInitContext). Free with swsFreeContext. @return SwsContext handle, 0. */
JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_swsAllocContext(JNIEnv *env, jobject thiz)
{
    return (jlong)(intptr_t)sws_alloc_context();
}

/* ------------------------------------------------------------------ */
/* Software video render path: sws_scale straight into an              */
/* ANativeWindow buffer.                                               */
/*                                                                     */
/* Used as the fallback when MediaCodec hardware decoding is           */
/* unavailable. Scaling directly into the window buffer avoids the      */
/* byte[] -> Bitmap -> Canvas round trip a Java-side renderer would    */
/* need (two extra full-frame copies per frame).                       */
/*                                                                     */
/* The SwsContext is cached across frames and only rebuilt when the     */
/* source frame geometry/format or the window buffer size changes.      */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Output session: admission control and verifiable unbinding.         */
/*                                                                     */
/* Why this exists, from the FFmpeg side:                              */
/*                                                                     */
/*   ff_mediacodec_dec_close() (mediacodecdec_common.c) only calls     */
/*   ff_AMediaCodec_stop() when hw_buffer_count has reached 0. With a  */
/*   frame still outstanding it logs "Not stopping MediaCodec (there   */
/*   are buffers pending)" and closes anyway. Closing the decoder is    */
/*   therefore NOT proof that the driver stopped producing output:     */
/*   the codec object may still be alive and still own the Surface.    */
/*                                                                     */
/* hw_buffer_count lives in MediaCodecDecContext, which is private to  */
/* libavcodec and unreachable from JNI. So this layer keeps its own    */
/* count of what it handed out and refuses to call anything detached   */
/* until that count is zero. That count is the evidence; a return code */
/* from a teardown function is not.                                    */
/*                                                                     */
/* States:                                                             */
/*                                                                     */
/*   ACTIVE -> REVOKING -> DETACHED -> CLOSED                          */
/*                     \-> QUARANTINED                                 */
/*                                                                     */
/*   REVOKING     no new submission is admitted; operations that were  */
/*                already admitted are allowed to finish and their      */
/*                completion is counted, which is what makes DETACHED  */
/*                reachable at all.                                    */
/*   DETACHED     the in-flight count is zero AND the platform handle  */
/*                has been released. Only reachable from REVOKING when */
/*                the count is zero; never by waiting for a timeout.   */
/*   QUARANTINED  teardown could not be proven complete. The session   */
/*                keeps its handle, refuses every further use, and is   */
/*                never rebound. Leaking a handle is recoverable;      */
/*                touching a Surface after its owner is gone is not.    */
/* ------------------------------------------------------------------ */

#define FF_OUTPUT_ACTIVE       0
#define FF_OUTPUT_REVOKING     1
#define FF_OUTPUT_DETACHED     2
#define FF_OUTPUT_QUARANTINED  3
#define FF_OUTPUT_CLOSED       4

typedef struct FFOutputSession {
    /* Guards every field below. Held only for counting/state transitions,
     * never across a lock/post/render call, so a blocked producer cannot
     * block revoke. */
    pthread_mutex_t lock;

    int state;

    /* Operations admitted and not yet completed. Incremented under `lock`
     * at admission, decremented on completion. */
    int in_flight;

    /* What this session owns, so teardown knows what to release exactly
     * once. Set to NULL when released, which is what makes try_finish()
     * idempotent. */
    ANativeWindow *win;
} FFOutputSession;

/* Admission: returns 0 and bumps in_flight when the session accepts work,
 * or a negative AVERROR when it must not. Callers MUST pair a successful
 * admission with ff_output_session_exit(). */
static int ff_output_session_enter(FFOutputSession *s)
{
    int ret = 0;

    if (!s)
        return AVERROR(EINVAL);
    pthread_mutex_lock(&s->lock);
    if (s->state != FF_OUTPUT_ACTIVE)
        ret = AVERROR(EPERM);
    else
        s->in_flight++;
    pthread_mutex_unlock(&s->lock);
    return ret;
}

/* Completion: the in-flight count can only go down here, so DETACHED is
 * reachable exactly when the last admitted operation has finished. */
static void ff_output_session_exit(FFOutputSession *s)
{
    if (!s)
        return;
    pthread_mutex_lock(&s->lock);
    if (s->in_flight > 0)
        s->in_flight--;
    pthread_mutex_unlock(&s->lock);
}

static int ff_output_session_in_flight(FFOutputSession *s)
{
    int n;

    pthread_mutex_lock(&s->lock);
    n = s->in_flight;
    pthread_mutex_unlock(&s->lock);
    return n;
}

/* Begin revoking. From this point on nothing new is admitted; already
 * admitted work drains on its own thread. Returns the in-flight count.
 *
 * This deliberately does NOT wait. A producer blocked inside
 * ANativeWindow_lock() or a MediaCodec render call cannot be unblocked by
 * the JNI layer, so waiting here would mean waiting on a timeout -- and a
 * timeout is not evidence that the driver stopped. The caller polls
 * ff_output_session_try_finish() until it reports DETACHED, and may run
 * that wait on a background thread with its own deadline; a deadline that
 * expires becomes QUARANTINED, never DETACHED. */
static int ff_output_session_revoke(FFOutputSession *s)
{
    if (!s)
        return AVERROR(EINVAL);
    pthread_mutex_lock(&s->lock);
    if (s->state == FF_OUTPUT_ACTIVE)
        s->state = FF_OUTPUT_REVOKING;
    pthread_mutex_unlock(&s->lock);
    return ff_output_session_in_flight(s);
}

/* Attempt to complete teardown.
 *
 * Releasing the platform handle happens exactly once, and only when the
 * in-flight count is zero: releasing an ANativeWindow while a producer is
 * inside lock()/unlockAndPost() is the use-after-free this whole session
 * exists to prevent. Returns the resulting state. */
static int ff_output_session_try_finish(FFOutputSession *s)
{
    int state;

    if (!s)
        return FF_OUTPUT_QUARANTINED;
    pthread_mutex_lock(&s->lock);
    if (s->state == FF_OUTPUT_REVOKING && s->in_flight == 0) {
        if (s->win) {
            ANativeWindow_release(s->win);
            s->win = NULL;
        }
        s->state = FF_OUTPUT_DETACHED;
    }
    state = s->state;
    pthread_mutex_unlock(&s->lock);
    return state;
}

/* Give up on proving teardown. The handle is intentionally NOT released
 * here when operations may still be in flight: releasing it would produce
 * the very crash the session refuses to cause. The memory is leaked and the
 * session is marked unusable so it can never be reused or rebound. */
static int ff_output_session_quarantine(FFOutputSession *s)
{
    if (!s)
        return AVERROR(EINVAL);
    pthread_mutex_lock(&s->lock);
    if (s->state != FF_OUTPUT_DETACHED && s->state != FF_OUTPUT_CLOSED)
        s->state = FF_OUTPUT_QUARANTINED;
    pthread_mutex_unlock(&s->lock);
    return ff_output_session_in_flight(s);
}

static int ff_output_session_state(FFOutputSession *s)
{
    int state;

    if (!s)
        return FF_OUTPUT_CLOSED;
    pthread_mutex_lock(&s->lock);
    state = s->state;
    pthread_mutex_unlock(&s->lock);
    return state;
}

typedef struct FFWindowRenderer {
    ANativeWindow     *win;
    struct SwsContext *sws;
    int src_w, src_h, src_fmt;
    int dst_w, dst_h;
    /* Admission control for every operation that touches `win`. */
    FFOutputSession    session;
    int                session_ready;
} FFWindowRenderer;

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_windowRendererCreate(JNIEnv *env, jobject thiz, jobject surface)
{
    if (!surface)
        return 0;
    ANativeWindow *win = ANativeWindow_fromSurface(env, surface);
    if (!win)
        return 0;
    FFWindowRenderer *r = av_mallocz(sizeof(*r));
    if (!r) {
        ANativeWindow_release(win);
        return 0;
    }
    if (pthread_mutex_init(&r->session.lock, NULL) != 0) {
        ANativeWindow_release(win);
        av_free(r);
        return 0;
    }
    r->win = win;
    r->src_fmt = AV_PIX_FMT_NONE;
    r->session.win   = win;
    r->session.state = FF_OUTPUT_ACTIVE;
    r->session.in_flight = 0;
    r->session_ready = 1;
    return (jlong)(intptr_t)r;
}

/* Every window operation goes through admission. A revoked session admits
 * nothing even if the caller still holds the handle: that is what stops an
 * old binding from being used with a new Surface's authority. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_windowRendererSetSize(JNIEnv *env, jobject thiz,
                                                   jlong handle, jint width, jint height)
{
    FFWindowRenderer *r = PTR(FFWindowRenderer *, handle);
    int ret;

    if (!r || !r->win || width <= 0 || height <= 0)
        return AVERROR(EINVAL);
    ret = ff_output_session_enter(&r->session);
    if (ret < 0)
        return ret;

    ret = ANativeWindow_setBuffersGeometry(r->win, width, height,
                                           WINDOW_FORMAT_RGBA_8888);
    ff_output_session_exit(&r->session);
    return ret == 0 ? 0 : AVERROR_EXTERNAL;
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_windowRendererRender(JNIEnv *env, jobject thiz,
                                                  jlong handle, jlong frame)
{
    FFWindowRenderer *r = PTR(FFWindowRenderer *, handle);
    AVFrame *f = PTR(AVFrame *, frame);
    ANativeWindow_Buffer buf;
    int ret, scaled;

    if (!r || !r->win || !f || f->width <= 0 || f->height <= 0)
        return AVERROR(EINVAL);

    /* Admission wraps the whole lock/scale/post sequence: revoke must wait
     * for exactly this window to close before it may release the handle. */
    ret = ff_output_session_enter(&r->session);
    if (ret < 0)
        return ret;

    if (ANativeWindow_lock(r->win, &buf, NULL) != 0) {
        ff_output_session_exit(&r->session);
        return AVERROR_EXTERNAL;
    }
    if (!buf.bits || buf.width <= 0 || buf.height <= 0) {
        ANativeWindow_unlockAndPost(r->win);
        ff_output_session_exit(&r->session);
        return AVERROR_EXTERNAL;
    }

    if (!r->sws || r->src_w != f->width || r->src_h != f->height ||
        r->src_fmt != f->format || r->dst_w != buf.width || r->dst_h != buf.height) {
        r->sws = sws_getCachedContext(r->sws, f->width, f->height,
                                      (enum AVPixelFormat)f->format,
                                      buf.width, buf.height, AV_PIX_FMT_RGBA,
                                      SWS_BILINEAR, NULL, NULL, NULL);
        if (!r->sws) {
            ANativeWindow_unlockAndPost(r->win);
            ff_output_session_exit(&r->session);
            return AVERROR(ENOMEM);
        }
        r->src_w   = f->width;
        r->src_h   = f->height;
        r->src_fmt = f->format;
        r->dst_w   = buf.width;
        r->dst_h   = buf.height;
    }

    /* buf.stride counts pixels, not bytes; RGBA_8888 is 4 bytes per pixel. */
    uint8_t *dst_data[4] = { (uint8_t *)buf.bits, NULL, NULL, NULL };
    int dst_linesize[4]  = { buf.stride * 4, 0, 0, 0 };
    const uint8_t *src_data[4];
    for (int i = 0; i < 4; i++)
        src_data[i] = f->data[i];

    scaled = sws_scale(r->sws, src_data, f->linesize, 0, f->height,
                       dst_data, dst_linesize);
    ANativeWindow_unlockAndPost(r->win);
    ff_output_session_exit(&r->session);
    return scaled > 0 ? 0 : AVERROR_EXTERNAL;
}

/* Begin unbinding. Returns the number of operations still admitted (>= 0)
 * or a negative AVERROR. The caller polls windowRendererFinish() until it
 * reports DETACHED; a poll timeout must become QUARANTINE, never DETACHED. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_windowRendererRevoke(JNIEnv *env, jobject thiz, jlong handle)
{
    FFWindowRenderer *r = PTR(FFWindowRenderer *, handle);

    if (!r || !r->session_ready)
        return AVERROR(EINVAL);
    return ff_output_session_revoke(&r->session);
}

/* Try to complete teardown; returns an FF_OUTPUT_* state. DETACHED means the
 * in-flight count reached zero and the window handle is gone: at that point
 * no admitted operation can still touch the Surface. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_windowRendererFinish(JNIEnv *env, jobject thiz, jlong handle)
{
    FFWindowRenderer *r = PTR(FFWindowRenderer *, handle);

    if (!r || !r->session_ready)
        return FF_OUTPUT_CLOSED;
    return ff_output_session_try_finish(&r->session);
}

/* Give up on proving teardown. The session is unusable afterwards and must
 * be dropped rather than reused or rebound. Returns the operations that
 * were still admitted, for diagnostics. */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_windowRendererQuarantine(JNIEnv *env, jobject thiz, jlong handle)
{
    FFWindowRenderer *r = PTR(FFWindowRenderer *, handle);

    if (!r || !r->session_ready)
        return AVERROR(EINVAL);
    return ff_output_session_quarantine(&r->session);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_windowRendererGetState(JNIEnv *env, jobject thiz, jlong handle)
{
    FFWindowRenderer *r = PTR(FFWindowRenderer *, handle);

    if (!r || !r->session_ready)
        return FF_OUTPUT_CLOSED;
    return ff_output_session_state(&r->session);
}

/* Free the renderer. The window is only released here when teardown was
 * never attempted or already completed; if operations are still in flight
 * the handle is leaked on purpose -- see ff_output_session_quarantine(). */
JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_windowRendererFree(JNIEnv *env, jobject thiz, jlong handle)
{
    FFWindowRenderer *r = PTR(FFWindowRenderer *, handle);
    int state, in_flight;

    if (!r)
        return;
    if (r->sws)
        sws_freeContext(r->sws);

    if (r->session_ready) {
        state     = ff_output_session_state(&r->session);
        in_flight = ff_output_session_in_flight(&r->session);
        if (in_flight == 0) {
            /* Safe: nothing can still be inside a window call. */
            pthread_mutex_lock(&r->session.lock);
            if (r->session.win) {
                ANativeWindow_release(r->session.win);
                r->session.win = NULL;
            }
            r->win = NULL;
            r->session.state = FF_OUTPUT_CLOSED;
            pthread_mutex_unlock(&r->session.lock);
        } else {
            /* Releasing now would free a window a producer may still be
             * posting to. Leak it, and start the session no further work. */
            av_log(NULL, AV_LOG_ERROR,
                   "windowRendererFree: %d operation(s) still in flight "
                   "(state %d); leaking the window handle to avoid a "
                   "use-after-free\n", in_flight, state);
            pthread_mutex_lock(&r->session.lock);
            r->session.win = NULL;
            r->session.state = FF_OUTPUT_QUARANTINED;
            r->win = NULL;
            pthread_mutex_unlock(&r->session.lock);
        }
        pthread_mutex_destroy(&r->session.lock);
    } else if (r->win) {
        ANativeWindow_release(r->win);
    }
    av_free(r);
}

/* ------------------------------------------------------------------ */
/* AAC raw -> ADTS packer for audio passthrough / offload.             */
/*                                                                     */
/* Matroska (and most containers) carry AAC as raw access units with   */
/* the AudioSpecificConfig in codecpar->extradata, while Android's     */
/* ENCODING_AAC_LC direct/offload sinks expect ADTS-framed input.      */
/* FFmpeg has no bsf in the asc->adts direction (aac_adtstoasc is the  */
/* reverse), so the 7-byte header is written here, using libavcodec's  */
/* own bit-writer and MPEG-4 audio config parser. The field order is   */
/* copied from libavformat/adtsenc.c:adts_write_frame_header(); the    */
/* legality checks mirror adts_decode_extradata() minus the PCE        */
/* branch (chan_config == 0 is simply rejected, falling back to sw).   */
/* ------------------------------------------------------------------ */

#define ADTS_HEADER_SIZE 7
#define ADTS_MAX_FRAME_BYTES ((1 << 14) - 1)

typedef struct FFAacAdtsPacker {
    int objecttype;         /* m4ac.object_type - 1 */
    int sample_rate_index;  /* m4ac.sampling_index  */
    int channel_conf;       /* m4ac.chan_config     */
    int mpeg_id;            /* 0 = MPEG-4           */
} FFAacAdtsPacker;

/**
 * Parse the AudioSpecificConfig from codecpar->extradata of stream
 * {@code stream_index} and create a packer for it.
 *
 * @return an opaque FFAacAdtsPacker handle, or 0 when the stream cannot be
 *         ADTS-packed (no extradata, AOT > 4, sampling_index == 15,
 *         chan_config 0/>7). Free with aacAdtsPackerFree.
 */
JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_aacAdtsPackerCreate(JNIEnv *env, jobject thiz,
                                                 jlong formatCtx, jint streamIndex)
{
    AVFormatContext *s = PTR(AVFormatContext *, formatCtx);
    if (!s || streamIndex < 0 || streamIndex >= (jint)s->nb_streams)
        return 0;
    AVCodecParameters *par = s->streams[streamIndex]->codecpar;
    if (!par || par->codec_id != AV_CODEC_ID_AAC)
        return 0;
    if (par->extradata_size <= 0 || !par->extradata) {
        av_log(NULL, AV_LOG_WARNING, "aac adts: no extradata (AudioSpecificConfig), cannot pack\n");
        return 0;
    }

    MPEG4AudioConfig m4ac;
    memset(&m4ac, 0, sizeof(m4ac));
    int off = avpriv_mpeg4audio_get_config2(&m4ac, par->extradata,
                                            par->extradata_size, 1, NULL);
    if (off < 0) {
        av_log(NULL, AV_LOG_WARNING, "aac adts: AudioSpecificConfig parse failed (%d)\n", off);
        return 0;
    }

    int objecttype        = m4ac.object_type - 1;
    int sample_rate_index = m4ac.sampling_index;
    int channel_conf      = m4ac.chan_config;

    /* Legality checks mirror adts_decode_extradata(); each failure is logged
     * separately so "设备不支持" and "码流装不下 ADTS" can be told apart. */
    if (objecttype > 3) {
        av_log(NULL, AV_LOG_WARNING,
               "aac adts: MPEG-4 AOT %d is not allowed in ADTS\n", objecttype + 1);
        return 0;
    }
    if (sample_rate_index == 15) {
        av_log(NULL, AV_LOG_WARNING, "aac adts: escape sample rate index illegal in ADTS\n");
        return 0;
    }
    if (channel_conf > 7) {
        av_log(NULL, AV_LOG_WARNING,
               "aac adts: channelConfiguration %d > 7 not supported in ADTS\n", channel_conf);
        return 0;
    }
    if (channel_conf == 0) {
        /* chan_config == 0 requires a PCE in every frame; not implemented --
         * fall back to software decode instead. */
        av_log(NULL, AV_LOG_WARNING, "aac adts: channelConfig 0 (PCE-required) not supported\n");
        return 0;
    }

    FFAacAdtsPacker *p = av_mallocz(sizeof(*p));
    if (!p)
        return 0;
    p->objecttype        = objecttype;
    p->sample_rate_index = sample_rate_index;
    p->channel_conf      = channel_conf;
    p->mpeg_id           = 0; /* MPEG-4 */
    av_log(NULL, AV_LOG_INFO, "aac adts: objecttype=%d sample_rate_index=%d channel_conf=%d\n",
           objecttype, sample_rate_index, channel_conf);
    return (jlong)(intptr_t)p;
}

/**
 * Wrap one raw AAC access unit in an ADTS header and copy it out.
 * Output layout: [7-byte header][packet payload].
 *
 * @return total bytes written (7 + payload), or a negative AVERROR.
 */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_aacAdtsPackerWrap(JNIEnv *env, jobject thiz,
                                               jlong handle, jlong pkt,
                                               jbyteArray out, jint off, jint len)
{
    FFAacAdtsPacker *p = PTR(FFAacAdtsPacker *, handle);
    AVPacket *packet = PTR(AVPacket *, pkt);
    if (!p || !packet || !out || !packet->data || packet->size < 0)
        return AVERROR(EINVAL);

    jint frame_size = ADTS_HEADER_SIZE + packet->size;
    if (frame_size > ADTS_MAX_FRAME_BYTES || frame_size > len)
        return AVERROR(EINVAL);

    /* Field order copied from adts_write_frame_header(). */
    uint8_t hdr[ADTS_HEADER_SIZE];
    PutBitContext pb;
    init_put_bits(&pb, hdr, ADTS_HEADER_SIZE);

    /* adts_fixed_header */
    put_bits(&pb, 12, 0xfff);            /* syncword */
    put_bits(&pb, 1, p->mpeg_id);        /* ID */
    put_bits(&pb, 2, 0);                 /* layer */
    put_bits(&pb, 1, 1);                 /* protection_absent */
    put_bits(&pb, 2, p->objecttype);     /* profile_objecttype */
    put_bits(&pb, 4, p->sample_rate_index);
    put_bits(&pb, 1, 0);                 /* private_bit */
    put_bits(&pb, 3, p->channel_conf);   /* channel_configuration */
    put_bits(&pb, 1, 0);                 /* original_copy */
    put_bits(&pb, 1, 0);                 /* home */

    /* adts_variable_header */
    put_bits(&pb, 1, 0);                 /* copyright_identification_bit */
    put_bits(&pb, 1, 0);                 /* copyright_identification_start */
    put_bits(&pb, 13, frame_size);       /* aac_frame_length */
    put_bits(&pb, 11, 0x7ff);            /* adts_buffer_fullness */
    put_bits(&pb, 2, 0);                 /* number_of_raw_data_blocks_in_frame */
    flush_put_bits(&pb);

    (*env)->SetByteArrayRegion(env, out, off, ADTS_HEADER_SIZE, (const jbyte *)hdr);
    if (packet->size > 0)
        (*env)->SetByteArrayRegion(env, out, off + ADTS_HEADER_SIZE,
                                   packet->size, (const jbyte *)packet->data);
    return frame_size;
}

JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_aacAdtsPackerFree(JNIEnv *env, jobject thiz, jlong handle)
{
    FFAacAdtsPacker *p = PTR(FFAacAdtsPacker *, handle);
    if (p)
        av_free(p);
}

/* ------------------------------------------------------------------ */
/* Interrupt flag: lets Java abort a blocking av_read_frame /           */
/* avformat_open_input from another thread.                             */
/*                                                                      */
/* Without this, a demux thread parked in a socket read can only be     */
/* unblocked by the protocol's own rw_timeout. A player that tears down  */
/* its AVFormatContext while that read is still in flight frees the      */
/* URLContext underneath the blocked thread, and the thread then         */
/* dereferences a dangling &h->interrupt_callback (SIGBUS / SIGSEGV on   */
/* a garbage function pointer). Setting the flag makes FFmpeg's I/O      */
/* layer return AVERROR_EXIT promptly, so the owner can join the thread  */
/* before closing the context.                                          */
/*                                                                      */
/* The callback deliberately stays in C and never enters the JVM: it is  */
/* polled inside tight I/O loops, and an AttachCurrentThread round trip  */
/* per poll would be both slow and deadlock-prone during teardown.       */
/* ------------------------------------------------------------------ */

static int ff_jni_interrupt_cb(void *opaque)
{
    const FFInterruptFlag *f = (const FFInterruptFlag *)opaque;
    return f && f->aborted;
}

/* Allocate a cleared interrupt flag. Free with interruptFlagFree once no
 * AVFormatContext references it any more. @return handle, 0 on OOM. */
JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_interruptFlagCreate(JNIEnv *env, jobject thiz)
{
    return (jlong)(intptr_t)av_mallocz(sizeof(FFInterruptFlag));
}

/* Install the flag as ctx->interrupt_callback. Must be called before the
 * context starts doing I/O (i.e. before formatOpenInput for open-time
 * cancellation, or right after it for read-time cancellation).
 * @return 0 on success, negative AVERROR */
JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_formatSetInterruptFlag(JNIEnv *env, jobject thiz,
                                                    jlong ctx, jlong flag)
{
    AVFormatContext *c = PTR(AVFormatContext *, ctx);
    FFInterruptFlag *f = PTR(FFInterruptFlag *, flag);
    if (!c || !f)
        return AVERROR(EINVAL);
    c->interrupt_callback.callback = ff_jni_interrupt_cb;
    c->interrupt_callback.opaque   = f;
    return 0;
}

/* Raise the flag: in-flight and subsequent blocking I/O on any context
 * holding it fails with AVERROR_EXIT. Safe to call from any thread, and
 * safe to call repeatedly. */
JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_interruptFlagAbort(JNIEnv *env, jobject thiz, jlong flag)
{
    FFInterruptFlag *f = PTR(FFInterruptFlag *, flag);
    if (f)
        f->aborted = 1;
}

/* Free the flag. The caller must guarantee that no AVFormatContext still
 * points at it (close the contexts first), otherwise their callback would
 * read freed memory. */
JNIEXPORT void JNICALL
Java_org_ffmpeg_FFMpegNative_interruptFlagFree(JNIEnv *env, jobject thiz, jlong flag)
{
    FFInterruptFlag *f = PTR(FFInterruptFlag *, flag);
    if (f)
        av_free(f);
}

/* ------------------------------------------------------------------ */
/* Library init: initialize the FFmpeg network layer when the .so is   */
/* loaded, so http:// and https:// (mbedTLS TLS backend) work at       */
/* runtime on Android without the caller having to invoke               */
/* formatNetworkInit() explicitly first.                               */
/* ------------------------------------------------------------------ */
JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *vm, void *reserved)
{
    g_vm = vm;
    avformat_network_init();
    /* Register the global JavaVM so libavcodec's ff_jni_get_env() can resolve
     * it later; required by av_mediacodec_default_init/free. Passing NULL as
     * the log context is accepted by FFmpeg. */
    av_jni_set_java_vm((void *)vm, NULL);
    return JNI_VERSION_1_6;
}


/* MediaHub outbound contract v1. Must ship together with libcurl.c environment
 * isolation and aviobuf.c child-option propagation. Pure capability probe:
 * never open a URL to test a capability (that could leak before failing).
 */
JNIEXPORT jint JNICALL
Java_com_inlz_mediahub_core_player_network_FfmpegNetworkCapabilities_nativeContractVersion(
        JNIEnv *env, jclass clazz)
{
    void *opaque = NULL;
    const char *name;
    (void)env;
    (void)clazz;
    while ((name = avio_enum_protocols(&opaque, 0))) {
        if (!strcmp(name, "libcurl"))
            return 1;
    }
    return 0;
}
