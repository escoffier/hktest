cmake_minimum_required(VERSION 2.8)
set (CMAKE_CXX_STANDARD 14)

project(hktest)
include_directories(./hksdk/include)
#include_directories(./ffmpeg-3.4.2-win64-dev/include)
include_directories(.)
aux_source_directory(. SRCS )

#link_directories(  ${CMAKE_CURRENT_BINARY_DIR}/libs ${CMAKE_CURRENT_BINARY_DIR}/hksdk/libs ${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-3.4.2-win64-dev/lib)
link_directories(/opt/CH_HCNetSDK_V5.2.7.4_build20170606_Linux64/lib /opt/CH_HCNetSDK_V5.2.7.4_build20170606_Linux64/lib/HCNetSDKCom)
add_definitions(-DHKDEVICE_EXPORTS -DGLOG_NO_ABBREVIATED_SEVERITIES -DGOOGLE_GLOG_DLL_DECL= -D__x86_64__)

link_libraries(pthread glog avformat avcodec avutil swresample z )
#link_libraries(/opt/CH_HCNetSDK_V5.2.7.4_build20170606_Linux64/lib/libhcnetsdk.so)
add_executable(${PROJECT_NAME} ${SRCS} )
target_link_libraries( ${PROJECT_NAME} libhcnetsdk.so)
#target_link_libraries(${PROJECT_NAME} pthread glog swresample avformat avcodec avutil z)
