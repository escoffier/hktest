#ifndef __RTMP_PUBLISHER__
#define __RTMP_PUBLISHER__
#include<string>

class RtmpPublisher
{
    public:
        RtmpPublisher() {}
        ~RtmpPublisher() ;
        bool connect(std::string rtmpurl);

        void sendH264Data(char* frames, int frames_size, uint32_t dts, uint32_t pts);
    private:
        void* srs_rtmp_;
};
#endif