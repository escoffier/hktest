#ifndef HKCAMARA_H
#define HKCAMARA_H
#include <string>
#include "threadsafequeue.h"
using namespace std;
#include <thread>

//class FILE;
class PsParser;
class DeviceIO;
class AVDemuxer;

class HkCamera
{
public:
    HkCamera() ;
    ~HkCamera();

    bool logIn(std::string id, std::string ip, int port, std::string name, std::string pwd);
    bool openRealStream();

    static void initSdk();

    void writeBuffer(unsigned char* data, int len);

private:
    string id_;
    string ip_;
    int    port_;
    string userName_;
    string pwd_;

    long sdkUserID_;
    long playHandle_;
public:
    FILE * file_;
    PsParser *parser;
    DeviceIO *dio;
    AVDemuxer * demuxer;
    FILE * rawfile_;

};
#endif // HKCAMARA_H
