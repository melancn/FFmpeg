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
#include <stdlib.h>
#include <string.h>

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

#include "libavformat/avformat.h"
#include "libavformat/avio.h"

#include "libavfilter/avfilter.h"
#include "libavfilter/buffersrc.h"
#include "libavfilter/buffersink.h"

#include "libavutil/mathematics.h"
#include "libavutil/display.h"

#include "libswscale/swscale.h"

#include "libswresample/swresample.h"

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
    if (!p || !out || !p->data || p->size <= 0 || len <= 0)
        return -1;
    jint n = (p->size < len) ? p->size : len;
    (*env)->SetByteArrayRegion(env, out, off, n, (const jbyte *)p->data);
    return n;
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

/* Copy decoded audio frame bytes. For interleaved (non-planar) formats the
 * whole buffer is copied from data[0]; for planar formats only plane 0 is
 * copied (use frameCopyAudioPlane for individual channels). Returns bytes
 * copied, or AVERROR for planar layouts when called via this entry. */
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
    int total;
    if (!av_sample_fmt_is_planar(fmt)) {
        total = f->linesize[0];
    } else {
        total = f->nb_samples * bps;
    }
    if (total <= 0 || ch <= 0 || bps <= 0)
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

JNIEXPORT jlong JNICALL
Java_org_ffmpeg_FFMpegNative_mediasurfaceGetBuffer(JNIEnv *env, jobject thiz, jlong frame)
{
    AVFrame *f = PTR(AVFrame *, frame);
    if (!f || f->format != AV_PIX_FMT_MEDIACODEC)
        return 0;
    return (jlong)(intptr_t)f->data[3];
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_mediasurfaceReleaseBuffer(JNIEnv *env, jobject thiz,
                                                       jlong buf, jint render)
{
    AVMediaCodecBuffer *b = PTR(AVMediaCodecBuffer *, buf);
    if (!b)
        return AVERROR(EINVAL);
    return (jint)av_mediacodec_release_buffer(b, render);
}

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_mediasurfaceRenderBufferAtTime(JNIEnv *env, jobject thiz,
                                                            jlong buf, jlong nanoTime)
{
    AVMediaCodecBuffer *b = PTR(AVMediaCodecBuffer *, buf);
    if (!b)
        return AVERROR(EINVAL);
    return (jint)av_mediacodec_render_buffer_at_time(b, (int64_t)nanoTime);
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

JNIEXPORT jint JNICALL
Java_org_ffmpeg_FFMpegNative_swrConvert(JNIEnv *env, jobject thiz, jlong ctx,
                                        jbyteArray out, jint outCount,
                                        jbyteArray in, jint inCount)
{
    struct SwrContext *c = PTR(struct SwrContext *, ctx);
    if (!c || !out)
        return AVERROR(EINVAL);

    int64_t out_layout = 0, out_fmt_i = 0;
    av_opt_get_int(c, "out_channel_layout", 0, &out_layout);
    av_opt_get_int(c, "out_sample_fmt",     0, &out_fmt_i);
    enum AVSampleFormat out_fmt = (enum AVSampleFormat)(int)out_fmt_i;
    AVChannelLayout out_ch_layout;
    if (av_channel_layout_from_mask(&out_ch_layout, (uint64_t)out_layout) < 0) {
        av_channel_layout_default(&out_ch_layout, 0);
    }
    int out_ch = out_ch_layout.nb_channels;
    av_channel_layout_uninit(&out_ch_layout);
    int out_bps = av_get_bytes_per_sample(out_fmt);
    if (out_ch <= 0 || out_bps <= 0 || outCount <= 0)
        return AVERROR(EINVAL);
    int out_bytes = outCount * out_bps * out_ch;
    jbyte *out_ptr = (*env)->GetByteArrayElements(env, out, NULL);
    if (!out_ptr)
        return AVERROR(ENOMEM);

    uint8_t *out_bufs[1] = { (uint8_t *)out_ptr };
    uint8_t *in_bufs[1] = { NULL };
    const uint8_t *in_cb[1] = { NULL };
    jbyte *in_ptr = NULL;
    int ret;

    if (in && inCount > 0) {
        in_ptr = (*env)->GetByteArrayElements(env, in, NULL);
        if (!in_ptr) {
            (*env)->ReleaseByteArrayElements(env, out, out_ptr, 0);
            return AVERROR(ENOMEM);
        }
        in_bufs[0] = (uint8_t *)in_ptr;
        in_cb[0]   = (const uint8_t *)in_ptr;
    }

    ret = swr_convert(c, out_bufs, outCount,
                      in ? (const uint8_t *const *)in_cb : NULL,
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
    av_buffer_unref(&cc->hw_device_ctx);
    AVBufferRef *copy = av_buffer_ref(ref);
    if (!copy) {
        cc->hw_device_ctx = NULL;
        return AVERROR(ENOMEM);
    }
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

