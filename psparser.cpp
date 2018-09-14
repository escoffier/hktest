#include "psparser.h"
#include <memory>
#include <iostream>
#include <buffer.h>
#include <glog/logging.h>

#define MNALU_START_CODE 0x00000001
#define MPACK_START_CODE 0x00 00 01 BA
#define MSYSTEM_HEADER_START_CODE 0x00 00 01 BB
#define MPROMGRAM_STREAM_MAP_START_CODE 0x00 00 01 BC
#define MPES_START_CODE_PREFIX 0x00 00 01



#define PACK_START_CODE             ((unsigned int)0x000001ba)
#define SYSTEM_HEADER_START_CODE    ((unsigned int)0x000001bb)
#define SEQUENCE_END_CODE           ((unsigned int)0x000001b7)
#define PACKET_START_CODE_MASK      ((unsigned int)0xffffff00)
#define PACKET_START_CODE_PREFIX    ((unsigned int)0x00000100)
#define ISO_11172_END_CODE          ((unsigned int)0x000001b9)

/* mpeg2 */
#define PROGRAM_STREAM_MAP 0x1bc
#define PRIVATE_STREAM_1   0x1bd
#define PADDING_STREAM     0x1be
#define PRIVATE_STREAM_2   0x1bf

#define AV_NOPTS_VALUE          ((int64_t)UINT64_C(0x8000000000000000))

PsParser::PsParser()
{
    last_sync = 0;
    last_sync = 0;
    io_ = nullptr;
    pb = new Buffer(1024);
}

PsParser::~PsParser()
{

}

PsParser::PsParser(FILE *file)
{
    file_ = file;
    last_sync = 0;
    io_ = nullptr;
    pb = new Buffer(1024);
}

#define MAX_SYNC_SIZE 100000

int PsParser::find_next_start_code(int *size_ptr, int32_t *header_state)
{
    unsigned int state, v;
    int val, n;

    state = *header_state;
    n = *size_ptr;

    //int index = 0;
    LOG(INFO)<<"11--n: "<<n<<std::endl;
    while (n > 0) {
            if (pb->eof()){
                LOG(INFO)<<"eof"<<std::endl;
                    break;
            }
            v = pb->avio_r8();
            n--;
            //std::cout<<"n: "<<n<<std::endl;
            if (state == 0x000001) {
                    state = ((state << 8) | v) & 0xffffff;
                    val = state;
                    goto found;
            }
            state = ((state << 8) | v) & 0xffffff; //reserve v
    }
    LOG(INFO)<<"not found start code"<<std::endl;
    val = -1;

found:
    //std::cout<<"22--n: "<<n<<std::endl;
    *header_state = state;
    *size_ptr = n;

    //std::cout<<"####find_next_start_code: "<<std::hex<<val<<"----"<<state<<std::oct<<"-----size_ptr: "<<n<<std::endl;
    LOG(INFO)<<"33--n: "<<n<<std::endl;
    return val;
}

long PsParser::mpegps_psm_parse(unsigned char *psm_es_type )
{
    int psm_length, ps_info_length, es_map_length;

    psm_length = pb->avio_rb16();
    pb->avio_r8();
    pb->avio_r8();
    ps_info_length = pb->avio_rb16();

    /* skip program_stream_info */
    pb->avio_skip( ps_info_length);
    /*es_map_length = */pb->avio_rb16();
    /* Ignore es_map_length, trust psm_length */
    es_map_length = psm_length - ps_info_length - 10;

    /* at least one es available? */
    while (es_map_length >= 4) {
            unsigned char type = pb->avio_r8();
            unsigned char es_id = pb->avio_r8();
            uint16_t es_info_length = pb->avio_rb16();

            /* remember mapping from stream id to stream type */
            psm_es_type[es_id] = type;
            //*psm_es_type = type;
            /* skip program_stream_info */
            pb->avio_skip(es_info_length);
            es_map_length -= 4 + es_info_length;
    }
    pb->avio_rb32(); /* crc32 */
    return 2 + psm_length;
}

int64_t PsParser::get_pts( int c)
{
    LOG(INFO)<<"get_pts: "<<c;
    uint8_t buf[5];

    buf[0] = c < 0 ? pb->avio_r8() : c;
    //avio_read(pb, buf + 1, 4);
    //memcpy(buf+1 , pb->buf_ptr, 4);
    //io_->blocking_read(buf+1, 4);
    pb->avio_read(buf + 1, 4);

    return ff_parse_pes_pts(buf);
}

int PsParser::find_pack_header()
{
    int header_state = 0xff;
    //int size = pb->buffer_size;
    int32_t size = pb->buf_end - pb->buffer;
    int startcode = find_next_start_code(&size, &header_state);
    //last_sync = pb->tell();
    if (startcode < 0) {
        if (pb->eof())
            return -1;
        // FIXME we should remember header_state
        //return FFERROR_REDO;
        return -1;
    }

    if (startcode == PACK_START_CODE)
    {
        DLOG(INFO)<<"***********find pack header: "<<size;
        return 0;
    }
    return 0;
}

int PsParser::read_pes_header(int64_t *ppos, int *pstart_code, int64_t *ppts, int64_t *pdts, bool *isNewPack)
{

    int len, size, startcode, c, flags, header_len;
    int32_t header_state;
    int pes_ext, ext2_len, id_ext, skip;
    unsigned char psm_es_type[256];
    //static int64_t last_sync = 0;

error_redo:
    DLOG(INFO) << "error_redo, last_sync: " <<last_sync;
    pb->seek(last_sync);
redo:
    header_state = 0xff;
    //size = pb->buffer_size;

    size = pb->buf_end - pb->buf_ptr;
    DLOG(INFO) << "---redo, remain size: " <<pb->remain_size()<<"--buf_ptr: "<<(void*)pb->buf_ptr<<"--buf_end: "<<(void*)pb->buf_end<< std::endl;
    startcode = find_next_start_code(&size, &header_state);
    last_sync = pb->tell();
    std::cout << "---redo, last_sync: " <<last_sync;
    if (startcode < 0) {
        DLOG(INFO) << "startcode less than 0: " <<std::hex<<startcode<<std::oct ;
        if (pb->eof())
            return -1;
        // FIXME we should remember header_state
        //return FFERROR_REDO;
        return -1;
    }

    if (startcode == PACK_START_CODE){
        *isNewPack = true;
        DLOG(INFO) << "PACK_START_CODE" ;
        goto redo;
    }
    if (startcode == SYSTEM_HEADER_START_CODE){
        DLOG(INFO) << "SYSTEM_HEADER_START_CODE" ;
        goto redo;
    }
    if (startcode == PADDING_STREAM) {
        DLOG(INFO) << "PADDING_STREAM" << std::endl;
        pb->avio_skip(pb->avio_rb16());
        goto redo;
    }
    if (startcode == PRIVATE_STREAM_2) {
        DLOG(INFO) << "PRIVATE_STREAM_2" ;
        goto redo;
    }

    if (startcode == PROGRAM_STREAM_MAP) {
        DLOG(INFO) << "PROGRAM_STREAM_MAP" ;
        mpegps_psm_parse(psm_es_type);
        goto redo;
    }

    /* find matching stream */
    if (!((startcode >= 0x1c0 && startcode <= 0x1df) ||
          (startcode >= 0x1e0 && startcode <= 0x1ef) ||
          (startcode == 0x1bd) ||
          (startcode == PRIVATE_STREAM_2) ||
          (startcode == 0x1fd)))
    {
        DLOG(INFO) << "find matching stream, startcode: "<<std::hex<<startcode <<std::oct;
        //int64_t size = pb->buf_end - pb->buf_ptr;
        DLOG(INFO) << "111---redo, remain size: " <<pb->remain_size()<<"--buf_ptr: "<<(void*)pb->buf_ptr<<"--buf_end: "<<(void*)pb->buf_end;
        goto redo;
    }


    if (ppos) {
        //*ppos = avio_tell(s->pb) - 4;
        *ppos = pb->buf_ptr - pb->buffer - 4;
    }

    int64_t pts, dts;
    //PES Packet length: 2 bytes
    len = pb->avio_rb16();
    pts = dts = AV_NOPTS_VALUE;
    if (startcode != PRIVATE_STREAM_2)
    {
        /* stuffing */
        DLOG(INFO)<<"stuffing";
        for (;;) {
            if (len < 1){
                DLOG(INFO)<<"111";
                goto error_redo;
            }
            c = pb->avio_r8();
            len--;
            /* XXX: for MPEG-1, should test only bit 7 */
            if (c != 0xff)
                break;
        }
        if ((c & 0xc0) == 0x40) {
            /* buffer scale & size */
            pb->avio_r8();
            c = pb->avio_r8();
            len -= 2;
        }
        if ((c & 0xe0) == 0x20) {
            dts =
                    pts = get_pts( c);
            len -= 4;
            if (c & 0x10) {
                dts = get_pts( -1);
                len -= 5;
            }
        }
        else if ((c & 0xc0) == 0x80) {
            /* mpeg 2 PES */
            //PTS DTS indicator	2  11 = both present, 01 is forbidden, 10 = only PTS, 00 = no PTS or DTS
            //ESCR flag	1
            //ES rate flag	1
            // DSM trick mode flag	1
            //Additional copy info flag	1
            //CRC flag	1
            //extension flag	1
            flags = pb->avio_r8();

            //PES header length	8 bits
            header_len = pb->avio_r8();
            len -= 2;
            if (header_len > len){
                DLOG(INFO)<<"222";
                goto error_redo;
            }
            len -= header_len;
            if (flags & 0x80) {
                dts = pts = get_pts( -1);
                header_len -= 5;
                if (flags & 0x40) {
                    dts = get_pts( -1);
                    header_len -= 5;
                }
            }
            if (flags & 0x3f && header_len == 0) {
                flags &= 0xC0;
                std::cout << "Further flags set but no bytes left\n";
                //av_log(s, AV_LOG_WARNING, "Further flags set but no bytes left\n");
            }
            if (flags & 0x01) { /* PES extension */
                pes_ext = pb->avio_r8();
                header_len--;
                /* Skip PES private data, program packet sequence counter
                            * and P-STD buffer */
                skip = (pes_ext >> 4) & 0xb;
                skip += skip & 0x9;
                if (pes_ext & 0x40 || skip > header_len) {
                    std::cout << "pes_ext " << std::hex << pes_ext << " is invalid\n" << pes_ext << std::endl;
                    //av_log(s, AV_LOG_WARNING, "pes_ext %X is invalid\n", pes_ext);
                    pes_ext = skip = 0;
                }
                pb->avio_skip( skip);
                header_len -= skip;

                if (pes_ext & 0x01) { /* PES extension 2 */
                    ext2_len = pb->avio_r8();
                    header_len--;
                    if ((ext2_len & 0x7f) > 0) {
                        id_ext = pb->avio_r8();
                        if ((id_ext & 0x80) == 0)
                            startcode = ((startcode & 0xff) << 8) | id_ext;
                        header_len--;
                    }
                }
            }
            if (header_len < 0){
                DLOG(INFO)<<"333";
                goto error_redo;
            }
            pb->avio_skip( header_len);
        }
        else if (c != 0xf)
            goto redo;
    }

    if (startcode == PRIVATE_STREAM_1) {
         DLOG(INFO)<<"PRIVATE_STREAM_1";
        startcode = pb->avio_r8();
        len--;
    }
    if (len < 0){
        DLOG(INFO)<<"444";
        goto error_redo;
    }

    *pstart_code = startcode;
    *ppts = pts;
    *pdts = dts;
    return len;
}

int PsParser::read_pes_header1(int64_t *ppos, int *pstart_code, int64_t *ppts, int64_t *pdts, bool *isNewPack)
{
    int len, size, startcode, c, flags, header_len;

    int pes_ext, ext2_len, id_ext, skip;
    unsigned char psm_es_type[256];
    //static int64_t last_sync = 0;
    //unsigned char buffer[1024] = 0;
    //io_->blocking_read(buffer, 1024);

error_redo:
    LOG(INFO) << "error_redo, last_sync: " <<pb->tell();
    //pb->seek(last_sync);
redo:
    header_state = 0xff;
    //size = pb->buffer_size;

    size = 1024;
    //std::cout << "---redo, remain size: " <<buffer->remain_size()<<"--buf_ptr: "<<(void*)pb->buf_ptr<<"--buf_end: "<<(void*)pb->buf_end<< std::endl;
    startcode = find_next_start_code( &size, &header_state);
    //last_sync = pb->tell();
    //std::cout << "---redo, last_sync: " <<last_sync<< std::endl;
    if (startcode < 0) {
        LOG(INFO) << "startcode less than 0: " <<std::hex<<startcode<<std::oct;
//        if (buffer->eof())
//            return -1;
//        // FIXME we should remember header_state
//        //return FFERROR_REDO;
//        return -1;
   }

    if (startcode == PACK_START_CODE){
        *isNewPack = true;
         LOG(INFO) << "PACK_START_CODE" ;
        goto redo;
    }
    if (startcode == SYSTEM_HEADER_START_CODE){
         LOG(INFO) << "SYSTEM_HEADER_START_CODE" ;
        goto redo;
    }
    if (startcode == PADDING_STREAM) {
         LOG(INFO) << "PADDING_STREAM" ;
        pb->avio_skip(pb->avio_rb16());
        goto redo;
    }
    if (startcode == PRIVATE_STREAM_2) {
         LOG(INFO) << "PRIVATE_STREAM_2" ;
        goto redo;
    }

    if (startcode == PROGRAM_STREAM_MAP) {
         LOG(INFO) << "PROGRAM_STREAM_MAP" ;
        mpegps_psm_parse(psm_es_type);
        goto redo;
    }

    /* find matching stream */
    if (!((startcode >= 0x1c0 && startcode <= 0x1df) ||
          (startcode >= 0x1e0 && startcode <= 0x1ef) ||
          (startcode == 0x1bd) ||
          (startcode == PRIVATE_STREAM_2) ||
          (startcode == 0x1fd)))
    {
         LOG(INFO) << "find matching stream, startcode: "<<std::hex<<startcode <<std::oct;
        //int64_t size = pb->buf_end - pb->buf_ptr;
        //std::cout << "111---redo, remain size: " <<pb->remain_size()<<"--buf_ptr: "<<(void*)pb->buf_ptr<<"--buf_end: "<<(void*)pb->buf_end<< std::endl;
        goto redo;
    }


//    if (ppos) {
//        //*ppos = avio_tell(s->pb) - 4;
//        *ppos = pb->buf_ptr - pb->buffer - 4;
//    }

    int64_t pts, dts;
    //PES Packet length: 2 bytes
    len = pb->avio_rb16();
    LOG(INFO)<<"PES length: "<<len;
    pts = dts = AV_NOPTS_VALUE;
    if (startcode != PRIVATE_STREAM_2)
    {
        /* stuffing */
         LOG(INFO)<<"start stuffing";
        for (;;) {
            if (len < 1){
                 LOG(INFO)<<"111";
                goto error_redo;
            }
            c = pb->avio_r8();
            len--;
            /* XXX: for MPEG-1, should test only bit 7 */
            if (c != 0xff)
                break;
        }
        if ((c & 0xc0) == 0x40) {
            /* buffer scale & size */
            pb->avio_r8();
            c = pb->avio_r8();
            len -= 2;
        }
        if ((c & 0xe0) == 0x20) {
            dts =
                    pts = get_pts( c);
            len -= 4;
            if (c & 0x10) {
                dts = get_pts( -1);
                len -= 5;
            }
        }
        else if ((c & 0xc0) == 0x80) {
            /* mpeg 2 PES */
            //PTS DTS indicator	2  11 = both present, 01 is forbidden, 10 = only PTS, 00 = no PTS or DTS
            //ESCR flag	1
            //ES rate flag	1
            // DSM trick mode flag	1
            //Additional copy info flag	1
            //CRC flag	1
            //extension flag	1
            flags = pb->avio_r8();

            //PES header length	8 bits
            header_len = pb->avio_r8();

            len -= 2;
            if (header_len > len){
                std::cout<<"222";
                goto error_redo;
            }
            len -= header_len;
            if (flags & 0x80) {
                dts = pts = get_pts( -1);
                header_len -= 5;
                if (flags & 0x40) {
                    dts = get_pts( -1);
                    header_len -= 5;
                }
            }
            if (flags & 0x3f && header_len == 0) {
                flags &= 0xC0;
                std::cout << "Further flags set but no bytes left\n";
                //av_log(s, AV_LOG_WARNING, "Further flags set but no bytes left\n");
            }
            if (flags & 0x01) { /* PES extension */
                pes_ext = pb->avio_r8();
                header_len--;
                /* Skip PES private data, program packet sequence counter
                            * and P-STD buffer */
                skip = (pes_ext >> 4) & 0xb;
                skip += skip & 0x9;
                if (pes_ext & 0x40 || skip > header_len) {
                    std::cout << "pes_ext " << std::hex << pes_ext << " is invalid\n" << pes_ext << std::endl;
                    //av_log(s, AV_LOG_WARNING, "pes_ext %X is invalid\n", pes_ext);
                    pes_ext = skip = 0;
                }
                pb->avio_skip( skip);
                header_len -= skip;

                if (pes_ext & 0x01) { /* PES extension 2 */
                    ext2_len = pb->avio_r8();
                    header_len--;
                    if ((ext2_len & 0x7f) > 0) {
                        id_ext = pb->avio_r8();
                        if ((id_ext & 0x80) == 0)
                            startcode = ((startcode & 0xff) << 8) | id_ext;
                        header_len--;
                    }
                }
            }
            if (header_len < 0){
                std::cout<<"333";
                goto error_redo;
            }
            LOG(INFO)<<"header_len: "<<header_len;
            pb->avio_skip( header_len);
        }
        else if (c != 0xf)
            goto redo;
    }

    if (startcode == PRIVATE_STREAM_1) {

        startcode = pb->avio_r8();
        len--;
         LOG(INFO)<<"PRIVATE_STREAM_1, startcode"<<std::hex<<startcode<<std::oct<<std::endl;
    }
    if (len < 0){
         LOG(INFO)<<"444";
        goto error_redo;
    }
    LOG(INFO)<<"stuffing end";
    *pstart_code = startcode;
    *ppts = pts;
    *pdts = dts;
    return len;
}

void PsParser::parse_pes()
{
//    int64_t pts, dts;
//    //PES Packet length: 2 bytes
//    len = pb->avio_rb16();
//    pts = dts = AV_NOPTS_VALUE;
//    if (startcode != PRIVATE_STREAM_2)
//    {
//        /* stuffing */
//        std::cout<<"stuffing"<<std::endl;
//        for (;;) {
//            if (len < 1){
//                std::cout<<"111"<<std::endl;
//                goto error_redo;
//            }
//            c = pb->avio_r8();
//            len--;
//            /* XXX: for MPEG-1, should test only bit 7 */
//            if (c != 0xff)
//                break;
//        }
//        if ((c & 0xc0) == 0x40) {
//            /* buffer scale & size */
//            pb->avio_r8();
//            c = pb->avio_r8();
//            len -= 2;
//        }
//        if ((c & 0xe0) == 0x20) {
//            dts =
//                    pts = get_pts( c);
//            len -= 4;
//            if (c & 0x10) {
//                dts = get_pts( -1);
//                len -= 5;
//            }
//        }
//        else if ((c & 0xc0) == 0x80) {
//            /* mpeg 2 PES */
//            //PTS DTS indicator	2  11 = both present, 01 is forbidden, 10 = only PTS, 00 = no PTS or DTS
//            //ESCR flag	1
//            //ES rate flag	1
//            // DSM trick mode flag	1
//            //Additional copy info flag	1
//            //CRC flag	1
//            //extension flag	1
//            flags = pb->avio_r8();

//            //PES header length	8 bits
//            header_len = pb->avio_r8();
//            len -= 2;
//            if (header_len > len){
//                std::cout<<"222"<<std::endl;
//                goto error_redo;
//            }
//            len -= header_len;
//            if (flags & 0x80) {
//                dts = pts = get_pts( -1);
//                header_len -= 5;
//                if (flags & 0x40) {
//                    dts = get_pts( -1);
//                    header_len -= 5;
//                }
//            }
//            if (flags & 0x3f && header_len == 0) {
//                flags &= 0xC0;
//                std::cout << "Further flags set but no bytes left\n";
//                //av_log(s, AV_LOG_WARNING, "Further flags set but no bytes left\n");
//            }
//            if (flags & 0x01) { /* PES extension */
//                pes_ext = pb->avio_r8();
//                header_len--;
//                /* Skip PES private data, program packet sequence counter
//                            * and P-STD buffer */
//                skip = (pes_ext >> 4) & 0xb;
//                skip += skip & 0x9;
//                if (pes_ext & 0x40 || skip > header_len) {
//                    std::cout << "pes_ext " << std::hex << pes_ext << " is invalid\n" << pes_ext << std::endl;
//                    //av_log(s, AV_LOG_WARNING, "pes_ext %X is invalid\n", pes_ext);
//                    pes_ext = skip = 0;
//                }
//                pb->avio_skip( skip);
//                header_len -= skip;

//                if (pes_ext & 0x01) { /* PES extension 2 */
//                    ext2_len = pb->avio_r8();
//                    header_len--;
//                    if ((ext2_len & 0x7f) > 0) {
//                        id_ext = pb->avio_r8();
//                        if ((id_ext & 0x80) == 0)
//                            startcode = ((startcode & 0xff) << 8) | id_ext;
//                        header_len--;
//                    }
//                }
//            }
//            if (header_len < 0){
//                std::cout<<"333"<<std::endl;
//                goto error_redo;
//            }
//            pb->avio_skip( header_len);
//        }
//        else if (c != 0xf)
//            goto redo;
//    }

}


int PsParser::mpegps_read_packet(/*, AVFormatContext *s, AVPacket *pkt*/)
{
    int len, startcode, i, es_type, ret;
    int lpcm_header_len = -1; //Init to suppress warning
    int request_probe= 0;
    enum AVCodecID codec_id = AV_CODEC_ID_NONE;
    enum AVMediaType type;
    int64_t pts, dts, dummy_pos; // dummy_pos is needed for the index building to work
    bool isNewPack;

    unsigned char buffer[1024* 50] = {0};
    //io_->blocking_read(pb->buffer, pb->buffer_size);

    //pb = new Buffer(buffer, 1024);
    //pb->fill_buffer();
    LOG(INFO)<<"***********mpegps_read_packet**************";

redo:
    len = read_pes_header1(&dummy_pos, &startcode, &pts, &dts, &isNewPack);
    if (len < 0)
        return len;

    if (startcode >= 0x80 && startcode <= 0xcf) {
        if (len < 4){
            //goto skip;
            /* skip packet */
            DLOG(INFO)<<"skip packet"<<std::endl;
            pb->avio_skip(len);
            //avio_skip(s->pb, len);
            goto redo;
        }

        /* audio: skip header */
        pb->avio_r8();
        lpcm_header_len = pb->avio_rb16();
        len -= 3;
        if (startcode >= 0xb0 && startcode <= 0xbf) {
            /* MLP/TrueHD audio has a 4-byte header */
            pb->avio_r8();
            //avio_r8(s->pb);
            len--;
        }
    }
//    auto st = streams.find(startcode);
//    if(st != streams.end()){

//    }

    if(startcode == 0x00){
        pb->avio_skip(len);
        goto redo;
    }
    int leftlen = pb->buf_end - pb->buf_ptr;
    LOG(INFO)<<"pts: " << pts <<"   dts: "<< dts << " len: "<<len<<" leftlen: "<<leftlen;
    if(len <= leftlen){
        fwrite(pb->buf_ptr, len, 1, file_ );
        pb->seek(len);
    }
    else {
        fwrite(pb->buf_ptr, leftlen, 1, file_ );
        pb->clear();

        io_->blocking_read(buffer, len - leftlen);
        fwrite(buffer, len - leftlen, 1, file_);
    }
    LOG(INFO)<<"buffer left len: "<<pb->buf_end - pb->buf_ptr;
//    if(len <= pb->buf_end - pb->buf_ptr){
//        //fwrite(pb->buf_ptr, len, 1, file_ );
//        last_sync = 0;
//        //pb->clear();
//    }else {
//        std::cout<<"No enough data"<<std::endl;;
//        return -1;
//    }

    return 0;
//skip:
//        /* skip packet */
//    pb->avio_skip(len);
//        //avio_skip(s->pb, len);
//    goto redo;


// found:
//    if (startcode >= 0xa0 && startcode <= 0xaf) {
//      if (st->second->codec_id == AV_CODEC_ID_MLP) {
//            if (len < 6)
//                goto skip;
//            avio_skip(pb, 6);
//            len -=6;
//      }
//    }

#if 0
    MpegDemuxContext *m = s->priv_data;
    AVStream *st;
    int len, startcode, i, es_type, ret;
    int lpcm_header_len = -1; //Init to suppress warning
    int request_probe= 0;
    enum AVCodecID codec_id = AV_CODEC_ID_NONE;
    enum AVMediaType type;
    int64_t pts, dts, dummy_pos; // dummy_pos is needed for the index building to work

redo:
    len = read_pes_header(s, &dummy_pos, &startcode, &pts, &dts);
    if (len < 0)
        return len;

    if (startcode >= 0x80 && startcode <= 0xcf) {
        if (len < 4)
            goto skip;

        /* audio: skip header */
        avio_r8(s->pb);
        lpcm_header_len = avio_rb16(s->pb);
        len -= 3;
        if (startcode >= 0xb0 && startcode <= 0xbf) {
            /* MLP/TrueHD audio has a 4-byte header */
            avio_r8(s->pb);
            len--;
        }
    }

    /* now find stream */
    for (i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];
        if (st->id == startcode)
            goto found;
    }

    es_type = m->psm_es_type[startcode & 0xff];
    if (es_type == STREAM_TYPE_VIDEO_MPEG1) {
        codec_id = AV_CODEC_ID_MPEG2VIDEO;
        type     = AVMEDIA_TYPE_VIDEO;
    } else if (es_type == STREAM_TYPE_VIDEO_MPEG2) {
        codec_id = AV_CODEC_ID_MPEG2VIDEO;
        type     = AVMEDIA_TYPE_VIDEO;
    } else if (es_type == STREAM_TYPE_AUDIO_MPEG1 ||
    es_type == STREAM_TYPE_AUDIO_MPEG2) {
        codec_id = AV_CODEC_ID_MP3;
        type     = AVMEDIA_TYPE_AUDIO;
    } else if (es_type == STREAM_TYPE_AUDIO_AAC) {
        codec_id = AV_CODEC_ID_AAC;
        type     = AVMEDIA_TYPE_AUDIO;
    } else if (es_type == STREAM_TYPE_VIDEO_MPEG4) {
        codec_id = AV_CODEC_ID_MPEG4;
        type     = AVMEDIA_TYPE_VIDEO;
    } else if (es_type == STREAM_TYPE_VIDEO_H264) {
        codec_id = AV_CODEC_ID_H264;
        type     = AVMEDIA_TYPE_VIDEO;
    } else if (es_type == STREAM_TYPE_AUDIO_AC3) {
        codec_id = AV_CODEC_ID_AC3;
        type     = AVMEDIA_TYPE_AUDIO;
    } else if (m->imkh_cctv && es_type == 0x91) {
        codec_id = AV_CODEC_ID_PCM_MULAW;
        type     = AVMEDIA_TYPE_AUDIO;
    } else if (startcode >= 0x1e0 && startcode <= 0x1ef) {
        static const unsigned char avs_seqh[4] = { 0, 0, 1, 0xb0 };
        unsigned char buf[8];

        avio_read(s->pb, buf, 8);
        avio_seek(s->pb, -8, SEEK_CUR);
        if (!memcmp(buf, avs_seqh, 4) && (buf[6] != 0 || buf[7] != 1))
            codec_id = AV_CODEC_ID_CAVS;
        else
            request_probe= 1;
        type = AVMEDIA_TYPE_VIDEO;
    } else if (startcode == PRIVATE_STREAM_2) {
        type = AVMEDIA_TYPE_DATA;
        codec_id = AV_CODEC_ID_DVD_NAV;
    } else if (startcode >= 0x1c0 && startcode <= 0x1df) {
        type     = AVMEDIA_TYPE_AUDIO;
        if (m->sofdec > 0) {
            codec_id = AV_CODEC_ID_ADPCM_ADX;
            // Auto-detect AC-3
            request_probe = 50;
        } else if (m->imkh_cctv && startcode == 0x1c0 && len > 80) {
            codec_id = AV_CODEC_ID_PCM_ALAW;
            request_probe = 50;
        } else {
            codec_id = AV_CODEC_ID_MP2;
            if (m->imkh_cctv)
                request_probe = 25;
        }
    } else if (startcode >= 0x80 && startcode <= 0x87) {
        type     = AVMEDIA_TYPE_AUDIO;
        codec_id = AV_CODEC_ID_AC3;
    } else if ((startcode >= 0x88 && startcode <= 0x8f) ||
    (startcode >= 0x98 && startcode <= 0x9f)) {
        /* 0x90 - 0x97 is reserved for SDDS in DVD specs */
        type     = AVMEDIA_TYPE_AUDIO;
        codec_id = AV_CODEC_ID_DTS;
    } else if (startcode >= 0xa0 && startcode <= 0xaf) {
        type     = AVMEDIA_TYPE_AUDIO;
        if (lpcm_header_len == 6 || startcode == 0xa1) {
            codec_id = AV_CODEC_ID_MLP;
        } else {
            codec_id = AV_CODEC_ID_PCM_DVD;
        }
    } else if (startcode >= 0xb0 && startcode <= 0xbf) {
        type     = AVMEDIA_TYPE_AUDIO;
        codec_id = AV_CODEC_ID_TRUEHD;
    } else if (startcode >= 0xc0 && startcode <= 0xcf) {
        /* Used for both AC-3 and E-AC-3 in EVOB files */
        type     = AVMEDIA_TYPE_AUDIO;
        codec_id = AV_CODEC_ID_AC3;
    } else if (startcode >= 0x20 && startcode <= 0x3f) {
        type     = AVMEDIA_TYPE_SUBTITLE;
        codec_id = AV_CODEC_ID_DVD_SUBTITLE;
    } else if (startcode >= 0xfd55 && startcode <= 0xfd5f) {
        type     = AVMEDIA_TYPE_VIDEO;
        codec_id = AV_CODEC_ID_VC1;
    } else {
skip:
        /* skip packet */
        avio_skip(s->pb, len);
        goto redo;
    }
    /* no stream found: add a new stream */
    st = avformat_new_stream(s, NULL);
    if (!st)
        goto skip;
    st->id                = startcode;
    st->codecpar->codec_type = type;
    st->codecpar->codec_id   = codec_id;
    if (   st->codecpar->codec_id == AV_CODEC_ID_PCM_MULAW
    || st->codecpar->codec_id == AV_CODEC_ID_PCM_ALAW) {
        st->codecpar->channels = 1;
        st->codecpar->channel_layout = AV_CH_LAYOUT_MONO;
        st->codecpar->sample_rate = 8000;
    }
    st->request_probe     = request_probe;
    st->need_parsing      = AVSTREAM_PARSE_FULL;

found:
    if (st->discard >= AVDISCARD_ALL)
        goto skip;
    if (startcode >= 0xa0 && startcode <= 0xaf) {
        if (st->codecpar->codec_id == AV_CODEC_ID_MLP) {
            if (len < 6)
                goto skip;
            avio_skip(s->pb, 6);
            len -=6;
        }
    }
    ret = av_get_packet(s->pb, pkt, len);

    pkt->pts          = pts;
    pkt->dts          = dts;
    pkt->pos          = dummy_pos;
    pkt->stream_index = st->index;

    if (s->debug & FF_FDEBUG_TS)
        av_log(s, AV_LOG_TRACE, "%d: pts=%0.3f dts=%0.3f size=%d\n",
        pkt->stream_index, pkt->pts / 90000.0, pkt->dts / 90000.0,
        pkt->size);

    return (ret < 0) ? ret : 0;
#endif
}


#define AV_RB16(x)                           \
    ((((const uint8_t*)(x))[0] << 8) |          \
      ((const uint8_t*)(x))[1])

int64_t PsParser::ff_parse_pes_pts(const uint8_t *buf)
{
    return (int64_t)(*buf & 0x0e) << 29 |
            (AV_RB16(buf + 1) >> 1) << 15 |
                                     AV_RB16(buf + 3) >> 1;
}

void PsParser::setIO(DeviceIO *io)
{
    io_ = io;
    pb->io_ = io;
}

void PsParser::start()
{
    paserThread = std::thread(&PsParser::run, this);
}

void PsParser::run()
{
    while (true) {
        mpegps_read_packet();
    }
    fflush(file_);
}
