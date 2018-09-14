#include "rtmppublisher.h"
#include "srs_librtmp.h"
#include "glog/logging.h"


 RtmpPublisher::~RtmpPublisher() 
 { 
     srs_rtmp_destroy(srs_rtmp_); 
}

bool RtmpPublisher::connect(std::string rtmpurl)
{
    srs_rtmp_ = srs_rtmp_create(rtmpurl.c_str());

    if (srs_rtmp_handshake(srs_rtmp_) != 0)
    {
        LOG(ERROR)<<"simple handshake failed.";
        return false;
    }

    if (srs_rtmp_connect_app(srs_rtmp_) != 0)
    {
        LOG(ERROR)<<"connect vhost/app failed.";
        return false;
    }

    if (srs_rtmp_publish_stream(srs_rtmp_) != 0)
    {
        LOG(ERROR) << "publish stream failed.";
        return false;
    }

    return true;
}

void RtmpPublisher::sendH264Data(char* frames, int frames_size, uint32_t dts, uint32_t pts)
{
    int ret = srs_h264_write_raw_frames(srs_rtmp_, frames, frames_size, dts, pts);
    if (ret != 0) 
    {
        if (srs_h264_is_dvbsp_error(ret)) {
                LOG(ERROR) << "ignore drop video error, code=" << ret;
            } else if (srs_h264_is_duplicated_sps_error(ret)) {
                LOG(ERROR) <<"ignore duplicated sps, code=" << ret;
            } else if (srs_h264_is_duplicated_pps_error(ret)) {
                LOG(ERROR) << "ignore duplicated pps, code="<< ret;
            } else {
                LOG(ERROR) << "send h264 raw data failed. ret=" << ret;
                //goto rtmp_destroy;
            }
    }
}