//
// Created by xulin on 2020/8/25.
//

#include <libavutil/imgutils.h>
#include <libavutil/timestamp.h>
#include "decode.h"

static AVPacket pkt;
static AVFrame *frame = NULL;
static int video_stream_idx = -1, audio_stream_idx = -1;
static AVCodecContext *video_dec_ctx = NULL, *audio_dec_ctx;
static int width, height;
static enum AVPixelFormat pix_fmt;
static int video_frame_count = 0;
static int audio_frame_count = 0;
static uint8_t *video_dst_data[4] = {NULL};
static int video_dst_linesize[4];
static int video_dst_bufsize;
#define INBUF_SIZE 4096
static void pgm_save(unsigned char *buf, int wrap, int xsize, int ysize,char *filename)
{
    FILE *f;
    int i;
    f = fopen(filename,"w");
    fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);
    for (i = 0; i < ysize; i++)
        fwrite(buf + i * wrap, 1, xsize, f);
    fclose(f);
}
static void decode(AVCodecContext *dec_ctx, AVFrame *frame, AVPacket *pkt,const char *filename)
{
    char buf[1024];
    int ret;
    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        fprintf(stderr, "Error sending a packet for decoding\n");
        exit(1);
    }
    while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error during decoding\n");
            exit(1);
        }
        printf("saving frame %3d\n", dec_ctx->frame_number);
        fflush(stdout);
        /* the picture is allocated by the decoder. no need to
           free it */
        snprintf(buf, sizeof(buf), "%s-%d", filename, dec_ctx->frame_number);
        pgm_save(frame->data[0], frame->linesize[0],
                 frame->width, frame->height, buf);
    }
}

static int open_codec_context(const char* filein,int *stream_idx,AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type)
{
    int ret, stream_index;
    AVStream *st;
    AVCodec *dec = NULL;
    AVDictionary *opts = NULL;

    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        LOGE( "Could not find %s stream in input file '%s'\n",
                av_get_media_type_string(type), filein);
        return ret;
    } else {
        stream_index = ret;
        st = fmt_ctx->streams[stream_index];

        /* find decoder for the stream */
        dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!dec) {
            LOGE( "Failed to find %s codec\n",
                    av_get_media_type_string(type));
            return AVERROR(EINVAL);
        }

        /* Allocate a codec context for the decoder */
        *dec_ctx = avcodec_alloc_context3(dec);
        if (!*dec_ctx) {
            LOGE("Failed to allocate the %s codec context\n",
                    av_get_media_type_string(type));
            return AVERROR(ENOMEM);
        }

        /* Copy codec parameters from input stream to output codec context */
        if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0) {
            LOGE( "Failed to copy %s codec parameters to decoder context\n",
                    av_get_media_type_string(type));
            return ret;
        }

        /* Init the decoders, with or without reference counting */
        av_dict_set(&opts, "refcounted_frames", refcount ? "1" : "0", 0);
        if ((ret = avcodec_open2(*dec_ctx, dec, &opts)) < 0) {
            LOGE( "Failed to open %s codec\n",
                    av_get_media_type_string(type));
            return ret;
        }
        *stream_idx = stream_index;
    }

    return 0;
}

static void log_callback(void *ptr, int level, const char *fmt, va_list vl) {
    static int print_prefix = 1;
    static char prev[1024];
    char line[1024];

    av_log_format_line(ptr, level, fmt, vl, line, sizeof(line), &print_prefix);

    strcpy(prev, line);
    if (level <= AV_LOG_WARNING) {
        LOGE("%s", line);
    } else {
        LOGD("%s", line);
    }
}


static int get_format_from_sample_fmt(const char **fmt,enum AVSampleFormat sample_fmt)
{
    int i;
    struct sample_fmt_entry {
        enum AVSampleFormat sample_fmt; const char *fmt_be, *fmt_le;
    } sample_fmt_entries[] = {
            { AV_SAMPLE_FMT_U8,  "u8",    "u8"    },
            { AV_SAMPLE_FMT_S16, "s16be", "s16le" },
            { AV_SAMPLE_FMT_S32, "s32be", "s32le" },
            { AV_SAMPLE_FMT_FLT, "f32be", "f32le" },
            { AV_SAMPLE_FMT_DBL, "f64be", "f64le" },
    };
    *fmt = NULL;

    for (i = 0; i < FF_ARRAY_ELEMS(sample_fmt_entries); i++) {
        struct sample_fmt_entry *entry = &sample_fmt_entries[i];
        if (sample_fmt == entry->sample_fmt) {
            *fmt = AV_NE(entry->fmt_be, entry->fmt_le);
            return 0;
        }
    }

    LOGE("sample format %s is not supported as output format\n",
            av_get_sample_fmt_name(sample_fmt));
    return -1;
}

static int decode_packet(int *got_frame, int cached,const char* video_out)
{
    int ret = 0;
    int decoded = pkt.size;

    *got_frame = 0;

    if (pkt.stream_index == video_stream_idx) {
        /* decode video frame */
        ret = avcodec_decode_video2(video_dec_ctx, frame, got_frame, &pkt);
        if (ret < 0) {
            LOGE("Error decoding video frame (%s)\n", av_err2str(ret));
            return ret;
        }

        if (*got_frame) {

            if (frame->width != width || frame->height != height ||
                frame->format != pix_fmt) {
                /* To handle this change, one could call av_image_alloc again and
                 * decode the following frames into another rawvideo file. */
                LOGE("Error: Width, height and pixel format have to be "
                                "constant in a rawvideo file, but the width, height or "
                                "pixel format of the input video changed:\n"
                                "old: width = %d, height = %d, format = %s\n"
                                "new: width = %d, height = %d, format = %s\n",
                        width, height, av_get_pix_fmt_name(pix_fmt),
                        frame->width, frame->height,
                        av_get_pix_fmt_name(frame->format));
                return -1;
            }

            char* fmt ;
            if(pix_fmt==AV_PIX_FMT_YUV420P){
                fmt = "AV_PIX_FMT_YUV420P";
            }
            else if(pix_fmt==AV_PIX_FMT_YUV422P){
                fmt = "AV_PIX_FMT_YUV422P";
            }
            else{
                fmt = "UNKNOWN";
            }

            LOGD("video_frame%s n:%d coded_n:%d pixel format:%s\n",
                   cached ? "(cached)" : "",
                   video_frame_count++, frame->coded_picture_number,fmt);

            //yuv data : frame->data;

            /* copy decoded frame to destination buffer:
             * this is required since rawvideo expects non aligned data */
//            av_image_copy(video_dst_data, video_dst_linesize,
//                          (const uint8_t **)(frame->data), frame->linesize,
//                          pix_fmt, width, height);

            /* write to rawvideo file */
            //fwrite(video_dst_data[0], 1, video_dst_bufsize, video_out);
        }
    } else if (pkt.stream_index == audio_stream_idx) {
        /* decode audio frame */
        ret = avcodec_decode_audio4(audio_dec_ctx, frame, got_frame, &pkt);
        if (ret < 0) {
            LOGE( "Error decoding audio frame (%s)\n", av_err2str(ret));
            return ret;
        }
        /* Some audio decoders decode only part of the packet, and have to be
         * called again with the remainder of the packet data.
         * Sample: fate-suite/lossless-audio/luckynight-partial.shn
         * Also, some decoders might over-read the packet. */
        decoded = FFMIN(ret, pkt.size);

        if (*got_frame) {
            size_t unpadded_linesize = frame->nb_samples * av_get_bytes_per_sample(frame->format);
            LOGD("audio_frame%s n:%d nb_samples:%d pts:%s\n",
                   cached ? "(cached)" : "",
                   audio_frame_count++, frame->nb_samples,
                   av_ts2timestr(frame->pts, &audio_dec_ctx->time_base));

            //PCM data: frame->extended_data;

            /* Write the raw audio data samples of the first plane. This works
             * fine for packed formats (e.g. AV_SAMPLE_FMT_S16). However,
             * most audio decoders output planar audio, which uses a separate
             * plane of audio samples for each channel (e.g. AV_SAMPLE_FMT_S16P).
             * In other words, this code will write only the first audio channel
             * in these cases.
             * You should use libswresample or libavfilter to convert the frame
             * to packed data. */
            //fwrite(frame->extended_data[0], 1, unpadded_linesize, audio_dst_file);
        }
    }

    /* If we use frame reference counting, we own the data and need
     * to de-reference it when we don't use it anymore */
    if (*got_frame && refcount)
        av_frame_unref(frame);

    return decoded;
}
int execute_decode(const char *filename,const char *outfilename)
{

    const AVCodec *codec;
    AVCodecParserContext *parser;
    AVCodecContext *c= NULL;
    AVFormatContext *fmt_ctx = NULL;

    AVStream *video_stream = NULL, *audio_stream = NULL;
    FILE *video_dst_file = NULL;


    int ret = 0, got_frame;


    av_log_set_flags(AV_LOG_SKIP_REPEATED);
    av_log_set_callback(log_callback);

    avformat_network_init();

    av_register_all();

    if(avformat_open_input(&fmt_ctx,filename,NULL,NULL)<0){
        LOGE("Could not open source file %s\n", filename);
        return -1;
    }
    if(avformat_find_stream_info(fmt_ctx,NULL)<0){
        LOGE("Could not find stream information\n");
        return -1;
    }
    if(open_codec_context(filename,&video_stream_idx,&video_dec_ctx,fmt_ctx,AVMEDIA_TYPE_VIDEO)>=0){
        video_stream  = fmt_ctx->streams[video_stream_idx];
        video_dst_file = fopen(outfilename,"wb");
        if(!video_dst_file){
            LOGE("Could not open destination file %s\n", outfilename);
            ret = 1;
            goto end;
        }

        /* allocate image where the decoded image will be put */
        width = video_dec_ctx->width;
        height = video_dec_ctx->height;
        pix_fmt = video_dec_ctx->pix_fmt;
        ret = av_image_alloc(video_dst_data, video_dst_linesize,
                             width, height, pix_fmt, 1);
        if (ret < 0) {
            LOGE("Could not allocate raw video buffer\n");
            goto end;
        }
        video_dst_bufsize = ret;
    }

    if(open_codec_context(filename,&audio_stream_idx,&audio_dec_ctx,fmt_ctx,AVMEDIA_TYPE_AUDIO)>=0){
        audio_stream = fmt_ctx->streams[audio_stream_idx];

    }

    av_dump_format(fmt_ctx,0,filename,0);

    if(!audio_stream&&!video_stream){
        LOGE( "Could not find audio or video stream in the input, aborting\n");
        ret = 1;
        goto end;
    }

    frame = av_frame_alloc();
    if(!frame){
        LOGE("Could not allocate frame\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    if (video_stream)
        LOGD("Demuxing video from file '%s' into '%s'\n", filename, outfilename);
    if (audio_stream)
        LOGD("Demuxing audio from file '%s'\n", filename);

    /* read frames from the file */
    while (av_read_frame(fmt_ctx, &pkt) >= 0) {
        AVPacket orig_pkt = pkt;
        do {
            ret = decode_packet(&got_frame, 0,outfilename);
            if (ret < 0)
                break;
            pkt.data += ret;
            pkt.size -= ret;
        } while (pkt.size > 0);
        av_packet_unref(&orig_pkt);
    }

    /* flush cached frames */
    pkt.data = NULL;
    pkt.size = 0;
    do {
        decode_packet(&got_frame, 1,outfilename);
    } while (got_frame);

    printf("Demuxing succeeded.\n");

    if (video_stream) {
        LOGD("Play the output video file with the command:\n"
             "ffplay -f rawvideo -pix_fmt %s -video_size %dx%d %s\n",
             av_get_pix_fmt_name(pix_fmt), width, height,
             outfilename);
    }

    if (audio_stream) {
        enum AVSampleFormat sfmt = audio_dec_ctx->sample_fmt;
        int n_channels = audio_dec_ctx->channels;
        const char *fmt;

        if (av_sample_fmt_is_planar(sfmt)) {
            const char *packed = av_get_sample_fmt_name(sfmt);
            LOGD("Warning: the sample format the decoder produced is planar "
                 "(%s). This example will output the first channel only.\n",
                 packed ? packed : "?");
            sfmt = av_get_packed_sample_fmt(sfmt);
            n_channels = 1;
        }

        if ((ret = get_format_from_sample_fmt(&fmt, sfmt)) < 0)
            goto end;

        LOGD("audio format:\n ffplay -f %s -ac %d -ar %d \n",
             fmt, n_channels, audio_dec_ctx->sample_rate);
    }

    end:
    avcodec_free_context(&video_dec_ctx);
    avcodec_free_context(&audio_dec_ctx);
    avformat_close_input(&fmt_ctx);
    if (video_dst_file)
        fclose(video_dst_file);

    av_frame_free(&frame);
    av_free(video_dst_data[0]);
    return 0;
}
