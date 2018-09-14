#include "hkcamara.h"
#include "HCNetSDK.h"
#include "glog/logging.h"
#include <iostream>
#include <time.h>
//#include "buffer.h"
#include "psparser.h"
#include "avdemuxer.h"

//unsigned char gbuf[1024*1024] = {0};
static Buffer buf(1024*1024);

FILE* file;
static void __stdcall RealDataCallBack(LONG lPlayHandle, DWORD dwDataType, BYTE *pBuffer, DWORD dwBufSize, void* pUser)
{

    static thread_local bool foundpes = false;
    //PsParser parser;
    std::thread::id this_id = std::this_thread::get_id();


    if (pUser == nullptr)
    {
        return;
    }

    HkCamera * camera = static_cast<HkCamera*>(pUser);
    switch (dwDataType)
    {
    case NET_DVR_SYSHEAD:
        break;

    case NET_DVR_STREAMDATA:
    {
        //LOG(INFO)<<"******receive data: "<< dwBufSize <<"   type: "<< dwDataType <<"  this_id: "<<this_id <<std::endl;
        //int64_t pts, dts, dummy_pos;
        //int startcode;
        fwrite(pBuffer, dwBufSize, 1, camera->rawfile_);
        //buf.write(pBuffer, dwBufSize);
        //parser.find_pack_header(&buf);
//        int len = parser.read_pes_header(&buf, &dummy_pos, &startcode, &pts, &dts);
//        if( len > 0)
//        {
//            foundpes = true;
//            std::cout<<"pts: "<<std::hex << pts <<"   dts: "<< dts << " len: "<<len<< std::endl;
//            buf.clear();
//        }

        //camera->parser->mpegps_read_packet(&buf);
        camera->dio->blocking_write(pBuffer, dwBufSize);
       // camera->demuxer->start2();
        //camera->writeBuffer(pBuffer, dwBufSize);
        //auto start = std::chrono::high_resolution_clock::now();
        //StreamManager::getInstance()->putDatatoBuffer("60000000001310001430", pBuffer, dwBufSize);
        //auto elapsed = std::chrono::high_resolution_clock::now() - start;
        //LOG(INFO) << "elapse  " << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() << " microseconds";
    }
    break;
    default:
        break;
    }
}

HkCamera::HkCamera()
{
    file_ = nullptr;
    rawfile_ = nullptr;

    rawfile_ = fopen("hkraw", "wb");
    time_t curtime;
    time(&curtime);
    string strTime = to_string(curtime);

    file_ = fopen(strTime.c_str(), "wb+");
    parser = new PsParser;
    parser->file_ = file_;

    dio = new DeviceIO;
    parser->setIO(dio);

    demuxer = new AVDemuxer;
    demuxer->setIO(dio);
    demuxer->file_ = file_;

}

HkCamera::~HkCamera()
{
    fflush(file_);
    fclose(file_);
}

bool HkCamera::logIn(std::string id, std::string ip, int port, std::string userName, std::string pwd)
{
        NET_DVR_USER_LOGIN_INFO struLoginInfo = { 0 };
        struLoginInfo.bUseAsynLogin = false;
        memcpy(struLoginInfo.sDeviceAddress, ip.c_str(), NET_DVR_DEV_ADDRESS_MAX_LEN);
        memcpy(struLoginInfo.sUserName, userName.c_str(), NAME_LEN);
        memcpy(struLoginInfo.sPassword, pwd.c_str(), PASSWD_LEN);
        struLoginInfo.wPort = port;

        NET_DVR_DEVICEINFO_V40 struDeviceInfoV40 = { 0 };

        sdkUserID_ = NET_DVR_Login_V40(&struLoginInfo, &struDeviceInfoV40);
        if (sdkUserID_ < 0)
        {
//            LOG(ERROR) << "注册设备失败:\r\n"
//                << "设备IP:[" << ip << "]\r\n"
//                << "设备端口:[" << port << "]\r\n"
//                << "用户名:[" << userName << "]\r\n"
//                << "密码:[" << pwd << "]\r\n"
//                << "错误码: " << NET_DVR_GetLastError();
            LOG(ERROR)<<"login camera failed "<<NET_DVR_GetLastError();
            return false;
        }

        LOG(INFO) << "Login " << ip << " successfully";

        NET_DVR_DEVICECFG_V40 devConfig;
        memset(&devConfig, 0, sizeof(LPNET_DVR_DEVICECFG_V40));
        DWORD  dwBytesReturned = 0;
        BOOL ret = NET_DVR_GetDVRConfig(sdkUserID_, NET_DVR_GET_DEVICECFG_V40, 0, &devConfig, sizeof(devConfig), &dwBytesReturned);
        LOG(INFO) << "device type : " << devConfig.byDevTypeName;

        return true;
}

bool HkCamera::openRealStream()
{
    if (sdkUserID_ < 0)
    {
        LOG(ERROR) << "Hik sdk open stream failed: invalied sdk user id ";
        return false;
    }

    if(file_ == nullptr){
        LOG(ERROR) << "Hik sdk open stream failed: buffer file is null ";
        return false;
    }



    NET_DVR_PREVIEWINFO struPlayInfo = { 0 };
    struPlayInfo.hPlayWnd = NULL; //需要 SDK 解码时句柄设为有效值，仅取流不解码时可设为空
    struPlayInfo.lChannel = 1;//
    struPlayInfo.dwLinkMode = 0;//0- TCP 方式， 1- UDP 方式， 2- 多播方式， 3- RTP 方式， 4-RTP/RTSP， 5-RSTP/HTTP
    struPlayInfo.dwStreamType = 0;//0-主码流， 1-子码流， 2-码流 3， 3-码流 4，以此类推
    //
    struPlayInfo.byPreviewMode = 0;//正常预览
    struPlayInfo.bBlocked = 1;//0- 非阻塞取流， 1- 阻塞取流
    struPlayInfo.bPassbackRecord = 0;//不启用录像回传;
    struPlayInfo.byProtoType = 0;//私有协议


    long playHandle_ = NET_DVR_RealPlay_V40(sdkUserID_, &struPlayInfo, RealDataCallBack, (void *)this);
    if (playHandle_ < 0)
    {
        LOG(ERROR) << "Hik sdk open stream failed, error code: " << NET_DVR_GetLastError();
        return false;
    }
    demuxer->start();
    //parser->start();
    return true;
}

void HkCamera::initSdk()
{
    NET_DVR_Init();
}

void HkCamera::writeBuffer(unsigned char *data, int len)
{

}
