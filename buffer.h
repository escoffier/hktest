#ifndef BUFFER_H
#define BUFFER_H
#include <cstdint>
#include <memory>
#include <iostream>
#include <mutex>
#include <atomic>
#include "glog/logging.h"
#include <condition_variable>

class DeviceIO
{
public:
    DeviceIO();
    ~DeviceIO();

    static int read_packet(void *opaque, uint8_t *buf, int buf_size);
    static int write_packet(void *opaque, uint8_t *buf, int buf_size);

    int blocking_read(uint8_t *buf, int buf_size);
    int blocking_write(uint8_t *buf, int buf_size);

    unsigned int read(uint8_t *buf, unsigned int buf_size);
    unsigned int write(uint8_t *buf, unsigned int buf_size);

    unsigned int available();

private:
    unsigned int avio_rb8();
    unsigned int avio_rb16();
    unsigned int avio_rb24();
    unsigned int avio_rb32();

private:
    unsigned char *buffer;
    unsigned int buffer_size;        /**< Maximum buffer size */
    unsigned char *read_ptr; /**< Current position in the buffer */
    unsigned char *write_ptr; /**< End of the data, may be less than
                                                        buffer+buffer_size if the read function returned
                                                        less data than requested, e.g. for streams where
                                                        no more data has been received yet. */
    //unsigned int write_pos;
    //unsigned int read_pos;
    std::atomic_uint write_pos;
    std::atomic_uint read_pos;
    //int size;             //current size in the buffer
    uint32_t promisedlen;
    std::mutex mutex_;
    std::condition_variable cond_;

};

struct Buffer
{
    Buffer(unsigned char *buf, int len):
        buffer(buf), buffer_size(len), buf_ptr(buf), buf_end(buf+ len), eof_reached(false), cur_pos(0)
    {}

    Buffer(int len);

    unsigned char *buffer;  /**< Start of the buffer. */
    int buffer_size;        /**< Maximum buffer size */
    unsigned char *buf_ptr; /**< Current position in the buffer */
    unsigned char *buf_end; /**< End of the data, may be less than
                                                        buffer+buffer_size if the read function returned
                                                        less data than requested, e.g. for streams where
                                                        no more data has been received yet. */

    bool eof_reached;
   int cur_pos;

    void write(unsigned char *buf, int len)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(buf_end + len < buffer + buffer_size)
        {
            memcpy(buf_end, buf, len);
            buf_end += len;
            int64_t size = buf_end - buf_ptr;
            std::cout<<"write size: "<<size<<std::endl;
        }
        else {
            std::cout<<"Buffer is full"<<std::endl;
        }
    }
    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        LOG(INFO)<<"xxxxxxxxxxx-----clear Buffer"<<std::endl;
        buf_ptr = buf_end;
    }
    unsigned int avio_r8()
    {
        if(buf_ptr >= buf_end )
        {
            fill_buffer();
        }

        if(buf_ptr < buf_end)
        {
            //++;
            return *buf_ptr++;
        }
        return 0;
    }

    int avio_read(unsigned char *buf, int size);

    void fill_buffer();

    unsigned int avio_rb16()
    {
        unsigned int val;
        val = avio_r8() << 8;
        val |= avio_r8();
        return val;
    }

    unsigned int avio_rb24( )
    {
        unsigned int val;
        val = avio_rb16() << 8;
        val |= avio_r8();
        return val;
    }
    unsigned int avio_rb32()
    {
        unsigned int val;
        val = avio_rb16() << 16;
        val |= avio_rb16();
        return val;
    }

    int64_t avio_skip(int64_t offset)
    {
        //std::lock_guard<std::mutex> lock(mutex_);
        LOG(INFO)<<"avio_skip: "<<offset<<std::endl;
        //return avio_seek(s, offset, SEEK_CUR);
        //min
//        buf_ptr += offset;
//        return offset;
        return seek(offset);
    }

    int64_t seek(int64_t offset)
    {
        //std::lock_guard<std::mutex> lock(mutex_);

        if (buf_ptr + offset < buf_end)
        {
            buf_ptr += offset;
            eof_reached = false;
            return offset;
        }
        else
        {
            int offset1  = offset - (buf_end- buf_ptr);
            fill_buffer();
            seek(offset1);
            return offset1;

//            buf_ptr = buf_end;
//            eof_reached = true;
//            return buf_end - buf_ptr;
        }

        LOG(INFO)<<"seek: "<<offset<<std::endl;
    }

    int64_t tell()
    {
        //std::lock_guard<std::mutex> lock(mutex_);
        return buf_ptr - buffer;
    }

    int64_t remain_size()
    {
        //std::lock_guard<std::mutex> lock(mutex_);
        return buf_end - buf_ptr;
    }

    bool eof()
    {
        //std::lock_guard<std::mutex> lock(mutex_);
        return eof_reached;
    }

    static int read_packet(void *opaque, uint8_t *buf, int buf_size);
    static int write_packet(void *opaque, uint8_t *buf, int buf_size);
    DeviceIO * io_;

    std::mutex mutex_;
};



struct Chunk
{
    Chunk() {}

};

struct Slabclass
{
    unsigned int size;      /* sizes of items */
    unsigned int perslab;   /* how many items per slab */

    void * slots_;           /* list of item ptrs */
    unsigned int sl_curr;   /* total free items in list */

    unsigned int slabs;     /* how many slabs were allocated for this class */

    void **slab_list;       /* array of slab pointers */
    unsigned int list_size; /* size of prev array */

    size_t requested; /* The number of requested bytes */
} ;

struct SlabContainer
{
    SlabContainer() {}

};
#endif // BUFFER_H
