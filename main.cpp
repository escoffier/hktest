#include <iostream>
#include "hkcamara.h"
#include "avdemuxer.h"
#include "glog/logging.h"
using namespace std;

int main(int argc, char **argv)
{
    google::InitGoogleLogging("argv");
    FLAGS_log_dir = "./log";
    FLAGS_stderrthreshold = 0;
    AVDemuxer::AVInitialize();

    HkCamera::initSdk();
    HkCamera camera;

    camera.logIn("122", "192.168.21.239", 8000, "admin", "dtnvs3000");
    camera.openRealStream();
    //cout << "Hello World!" << endl;
    while(true)
    {
        std::this_thread::sleep_for(1s);
    }
   // system("pause");
    return 0;
}
