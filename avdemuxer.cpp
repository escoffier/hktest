#ifdef __cplusplus

/*!
  NOTE: include this at last
 */
#define QTAV_USE_FFMPEG(MODULE) (MODULE##_VERSION_MICRO >= 100)
#define QTAV_USE_LIBAV(MODULE)  !QTAV_USE_FFMPEG(MODULE)
#define FFMPEG_MODULE_CHECK(MODULE, MAJOR, MINOR, MICRO) \
    (QTAV_USE_FFMPEG(MODULE) && MODULE##_VERSION_INT >= AV_VERSION_INT(MAJOR, MINOR, MICRO))
#define LIBAV_MODULE_CHECK(MODULE, MAJOR, MINOR, MICRO) \
    (QTAV_USE_LIBAV(MODULE) && MODULE##_VERSION_INT >= AV_VERSION_INT(MAJOR, MINOR, MICRO))
#define AV_MODULE_CHECK(MODULE, MAJOR, MINOR, MICRO, MINOR2, MICRO2) \
    (LIBAV_MODULE_CHECK(MODULE, MAJOR, MINOR, MICRO) || FFMPEG_MODULE_CHECK(MODULE, MAJOR, MINOR2, MICRO2))
/// example: AV_ENSURE(avcodec_close(avctx), false) will print error and return false if failed. AV_WARN just prints error.
#define AV_ENSURE_OK(FUNC, ...) AV_RUN_CHECK(FUNC, return, __VA_ARGS__)
#define AV_ENSURE(FUNC, ...) AV_RUN_CHECK(FUNC, return, __VA_ARGS__)
#define AV_WARN(FUNC) AV_RUN_CHECK(FUNC, void)

#define QTAV_HAVE(FEATURE) (defined QTAV_HAVE_##FEATURE && QTAV_HAVE_##FEATURE)
extern "C"
{
/*UINT64_C: C99 math features, need -D__STDC_CONSTANT_MACROS in CXXFLAGS*/
#endif /*__cplusplus*/
#include "libavformat/avformat.h"
#include <libswscale/swscale.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/avstring.h>
#include <libavutil/dict.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/mathematics.h> //AV_ROUND_UP, av_rescale_rnd for libav
#include <libavutil/cpu.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libavutil/parseutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/avstring.h>
#include <libavutil/timestamp.h>
#if !FFMPEG_MODULE_CHECK(LIBAVUTIL, 51, 73, 101)
#include <libavutil/channel_layout.h>
#endif

/* TODO: how to check whether we have swresample or not? how to check avresample?*/
#include <libavutil/samplefmt.h>
#if QTAV_HAVE(SWRESAMPLE)
#include <libswresample/swresample.h>
#ifndef LIBSWRESAMPLE_VERSION_INT //ffmpeg 0.9, swr 0.5
#define LIBSWRESAMPLE_VERSION_INT AV_VERSION_INT(LIBSWRESAMPLE_VERSION_MAJOR, LIBSWRESAMPLE_VERSION_MINOR, LIBSWRESAMPLE_VERSION_MICRO)
#endif //LIBSWRESAMPLE_VERSION_INT
//ffmpeg >= 0.11.x. swr0.6.100: ffmpeg-0.10.x
#define HAVE_SWR_GET_DELAY (LIBSWRESAMPLE_VERSION_INT > AV_VERSION_INT(0, 6, 100))
#endif //QTAV_HAVE(SWRESAMPLE)
#if QTAV_HAVE(AVRESAMPLE)
#include <libavresample/avresample.h>
#endif //QTAV_HAVE(AVRESAMPLE)

#if QTAV_HAVE(AVFILTER)
#include <libavfilter/avfiltergraph.h> /*code is here for old version*/
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#endif //QTAV_HAVE(AVFILTER)

#if QTAV_HAVE(AVDEVICE)
#include <libavdevice/avdevice.h>
#endif

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#include "avdemuxer.h"
#include "buffer.h"
#include <thread>
#include <time.h>
#include <string>
#include "glog/logging.h"
#include "rtmppublisher.h"

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag)
{
     DLOG(INFO)<<"pts: " /*<<std::hex*/ << pkt->pts <<"   dts: "<< pkt->dts << " pkt len: "<<pkt->size;
//    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

//    printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
//           tag,
//           av_ts2str(pkt->pts),
//           av_ts2timestr(pkt->pts, time_base),
//           av_ts2str(pkt->dts),
//           av_ts2timestr(pkt->dts, time_base),
//           av_ts2str(pkt->duration),
//           av_ts2timestr(pkt->duration, time_base),
//           pkt->stream_index);
}

FILE *logfile = fopen("ffmpeg.log", "w");

void logOutput(void* ptr, int level, const char* fmt, va_list vl) {
    if (logfile) {
        vfprintf(logfile, fmt, vl);
        fflush(logfile);
        //fclose(fp);
    }

}


AVDemuxer::AVDemuxer():
    ifmt_ctx(nullptr),ptCodecCtx(nullptr),ptCodecParserCtx(nullptr),videstream(-1), audiostream(-1), io_(nullptr),isStart(false),running(true)
{
    memset(inbuf, 0, sizeof(inbuf));
    file_ = nullptr;
    time_t curtime;
    time(&curtime);
    std::string strTime = std::to_string(curtime);

    file_ = fopen(strTime.c_str(), "wb+");

    rtmp = new RtmpPublisher;
}

AVDemuxer::~AVDemuxer()
{
    running = false;
    if(demuxerThread.joinable())
        demuxerThread.join();
}

void AVDemuxer::AVInitialize()
{
    //av_register_all();
    av_log_set_level(AV_LOG_TRACE);
    av_log_set_callback(logOutput);
}

#include <chrono>
using namespace std::chrono_literals;
void AVDemuxer::start()
{

    //std::this_thread::sleep_for(2s);
    if(isStart){
        return;
    }

    ifmt_ctx = avformat_alloc_context();

    unsigned char* buf = (unsigned char*)av_malloc(32768);
    AVIOContext *ioctx = avio_alloc_context(buf, 32768, 0, (void *)io_, DeviceIO::read_packet, NULL, NULL);

    ifmt_ctx->pb = ioctx;
    ifmt_ctx->probesize = 4096;
    ifmt_ctx->max_analyze_duration  = 2* AV_TIME_BASE;
    //ifmt_ctx->flags |= AVFMT_FLAG_NOBUFFER;
     //ifmt_ctx->interrupt_callback =
    auto ret = avformat_open_input(&ifmt_ctx, "", nullptr, nullptr);

    if(ret < 0){
        LOG(ERROR)<<"Could not open input file"<<std::endl;
        return;
    }

    auto start = std::chrono::high_resolution_clock::now();
    ret = avformat_find_stream_info(ifmt_ctx, nullptr);
    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    DLOG(INFO)<<"avformat_find_stream_info spend: "<<std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() << " microseconds";

    if(ret < 0){
        LOG(ERROR)<<"Could not open find stream info";
        return;
    }

    for (int i = 0; i < ifmt_ctx->nb_streams; i++)
    {
        if(ifmt_ctx->streams[i]->codecpar->codec_type = AVMEDIA_TYPE_VIDEO)
        {
            videstream = i;
        }else if(ifmt_ctx->streams[i]->codecpar->codec_type = AVMEDIA_TYPE_AUDIO)
        {
            audiostream = i;
        }
        else
        {
            LOG(WARNING)<<"stream is : "<<i<<"codec_type is : "<<ifmt_ctx->streams[i]->codecpar->codec_type;
        }
    }
    isStart = true;
    LOG(INFO)<<"Init ffmpeg contex successfull";

    rtmp->connect("rtmp://192.168.21.225/live/livestream");
    demuxerThread = std::thread(&AVDemuxer::read_packet, this);

}

void AVDemuxer::start2()
{
    if(isStart){
        return;
    }

    AVCodec* ptCodec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if(!ptCodec)
    {
        LOG(ERROR)<<"avcodec_find_decoder failed \n";
        return;
    }

    ptCodecParserCtx = av_parser_init(AV_CODEC_ID_H264);
    if(!ptCodecParserCtx)
    {
        LOG(ERROR)<<"av_parser_init failed \n";
        return;
    }

    ptCodecCtx = avcodec_alloc_context3(ptCodec);
    if(!ptCodecCtx)
    {
        LOG(ERROR)<<"avcodec_alloc_context3 failed \n";
        return;
    }

    int ret = avcodec_open2(ptCodecCtx, ptCodec, NULL);
    if(ret < 0)
    {
        LOG(ERROR)<<"avcodec_open2 failed \n";
        return;
    }

    isStart = true;
    LOG(INFO) << "begin to paser data " ;
    //std::cout<<"begin to paser data\n";
    demuxerThread = std::thread(&AVDemuxer::read_packet2, this);
}

void AVDemuxer::setIO(DeviceIO *io)
{
    io_ = io;
}

void AVDemuxer::read_packet()
{
//    AVPacket *pkt = av_packet_alloc();
//    memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);

//    av_init_packet(pkt);
//    pkt->data = NULL;
//    pkt->size = 0;
//    int datalen = 0;
//     uint8_t *data = inbuf;
//     int ret = 0;
//    while(running)
//    {

//        datalen = io_->blocking_read(inbuf, INBUF_SIZE);
//        LOG(INFO)<<"------datalen1: "<<datalen<<std::endl;
//        data = inbuf;
//        while(datalen > 0)
//        {
//            ret = av_parser_parse2(ptCodecParserCtx, ptCodecCtx, &pkt->data, &pkt->size,  data, datalen,
//                                               AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);

//            if(ret < 0){
//                LOG(ERROR) <<"Error while parsing";
//                //std::cout<<"Error while parsing\n";
//                break;
//            }

//            data += ret;
//            datalen -= ret;

//            if(pkt->size){

//                fwrite(pkt->data, pkt->size, 1, file_ );
//                LOG(INFO) <<"-----pts: "<<ptCodecParserCtx->pts<<"  dts: "<<ptCodecParserCtx->dts<<"  pkt size: "<<pkt->size
//                         <<"  datalen2: "<<datalen<<" ret: "<< ret;

//            }
//            else
//            {
//                LOG(INFO)<<"------skip---------"<<ret<<"----datalen: "<<datalen;
//                //std::this_thread::yield();
//            }
//        }

//    }

    AVPacket *pkt = av_packet_alloc();
    av_init_packet(pkt);
    pkt->data = nullptr;
    pkt->size = 0;

    while (true) {
        auto ret = av_read_frame(ifmt_ctx, pkt);
        if (ret < 0){
            LOG(ERROR)<<"av_read_frame error: "<<ret;
            break;
        }
        fwrite(pkt->data, pkt->size, 1, file_ );
        rtmp->sendH264Data((char *)pkt->data, pkt->size, pkt->pts/90, pkt->dts/90);
        log_packet(ifmt_ctx, pkt, "in");
        av_packet_unref(pkt);
    }
}

void AVDemuxer::read_packet2()
{
    AVPacket *pkt = av_packet_alloc();
    memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    av_init_packet(pkt);
    pkt->data = NULL;
    pkt->size = 0;
    int datalen = 0;
     uint8_t *data = inbuf;
     int ret = 0;
    while(running)
    {

        datalen = io_->blocking_read(inbuf, INBUF_SIZE);
        LOG(INFO)<<"------datalen1: "<<datalen<<std::endl;
        data = inbuf;
        while(datalen > 0)
        {
            ret = av_parser_parse2(ptCodecParserCtx, ptCodecCtx, &pkt->data, &pkt->size,  data, datalen,
                                               AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);

            if(ret < 0){
                LOG(ERROR) <<"Error while parsing";
                //std::cout<<"Error while parsing\n";
                break;
            }

            data += ret;
            datalen -= ret;

            if(pkt->size){

                fwrite(pkt->data, pkt->size, 1, file_ );
                LOG(INFO) <<"-----pts: "<<ptCodecParserCtx->pts<<"  dts: "<<ptCodecParserCtx->dts<<"  pkt size: "<<pkt->size
                         <<"  datalen2: "<<datalen<<" ret: "<< ret;

            }
            else
            {
                LOG(INFO)<<"------skip---------"<<ret<<"----datalen: "<<datalen;
                //std::this_thread::yield();
            }
        }

    }
}
