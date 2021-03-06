//
// Created by xulin on 2020/8/25.
//

#ifndef ANDROIDTRANSCODER_DECODE_H
#define ANDROIDTRANSCODER_DECODE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "android/log.h"

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "ffmpeg-decode", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ffmpeg-decode", __VA_ARGS__)
static int refcount = 0;

static void log_callback(void *ptr, int level, const char *fmt, va_list vl);
static int open_codec_context(const char* filein,int *stream_idx,AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type);

static int get_format_from_sample_fmt(const char **fmt,enum AVSampleFormat sample_fmt);
int execute_decode(const char *filename);
static int decode_packet(int *got_frame, int cached);

#endif //ANDROIDTRANSCODER_DECODE_H
