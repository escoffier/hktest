#ifndef AVDEMUXER_H
#define AVDEMUXER_H
#include <thread>
#include <atomic>

struct AVFormatContext;
struct AVCodecContext;
struct AVCodecParserContext;
class DeviceIO;
class RtmpPublisher;

#define INBUF_SIZE 4096

class AVDemuxer
{
public:
    AVDemuxer();
    ~AVDemuxer();

    static void AVInitialize();
    static int handleTimeout(void* obj);

    void start();
    void start2();
    void setIO(DeviceIO* io);
    void read_packet();
    void read_packet2();

private:
    AVFormatContext *ifmt_ctx;
    AVCodecContext* ptCodecCtx;
    AVCodecParserContext* ptCodecParserCtx;
    int videstream;
    int audiostream;
    DeviceIO* io_;
    bool isStart;
    std::thread demuxerThread;

    uint8_t inbuf[INBUF_SIZE + 32];
    std::atomic_bool running;
    RtmpPublisher * rtmp;

public:
    FILE * file_;
};

#endif // AVDEMUXER_H
