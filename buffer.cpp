#include "buffer.h"
#include <algorithm>
#include <time.h>
#include <string>
#include "glog/logging.h"

#define roundup_pow_of_two(n)   \
    (1UL    <<                             \
        (                                  \
            (                              \
            (n) & (1UL << 31) ? 31 :       \
            (n) & (1UL << 30) ? 30 :       \
            (n) & (1UL << 29) ? 29 :       \
            (n) & (1UL << 28) ? 28 :       \
            (n) & (1UL << 27) ? 27 :       \
            (n) & (1UL << 26) ? 26 :       \
            (n) & (1UL << 25) ? 25 :       \
            (n) & (1UL << 24) ? 24 :       \
            (n) & (1UL << 23) ? 23 :       \
            (n) & (1UL << 22) ? 22 :       \
            (n) & (1UL << 21) ? 21 :       \
            (n) & (1UL << 20) ? 20 :       \
            (n) & (1UL << 19) ? 19 :       \
            (n) & (1UL << 18) ? 18 :       \
            (n) & (1UL << 17) ? 17 :       \
            (n) & (1UL << 16) ? 16 :       \
            (n) & (1UL << 15) ? 15 :       \
            (n) & (1UL << 14) ? 14 :       \
            (n) & (1UL << 13) ? 13 :       \
            (n) & (1UL << 12) ? 12 :       \
            (n) & (1UL << 11) ? 11 :       \
            (n) & (1UL << 10) ? 10 :       \
            (n) & (1UL <<  9) ?  9 :       \
            (n) & (1UL <<  8) ?  8 :       \
            (n) & (1UL <<  7) ?  7 :       \
            (n) & (1UL <<  6) ?  6 :       \
            (n) & (1UL <<  5) ?  5 :       \
            (n) & (1UL <<  4) ?  4 :       \
            (n) & (1UL <<  3) ?  3 :       \
            (n) & (1UL <<  2) ?  2 :       \
            (n) & (1UL <<  1) ?  1 :       \
            (n) & (1UL <<  0) ?  0 : -1    \
            ) + 1                          \
        )                                  \
)

DeviceIO::DeviceIO():
    buffer(new uint8_t[1024*1024]), mutex_(),promisedlen(0)
{
    buffer_size = 1024*1024;
    write_pos = read_pos = 0;
}

DeviceIO::~DeviceIO()
{

}

int DeviceIO::read_packet(void *opaque, uint8_t *buf, int buf_size)
{
   DeviceIO *io = static_cast<DeviceIO*>(opaque);
   DLOG(INFO)<<"read_packet available "<<io->available() <<std::endl;
   return io->blocking_read(buf, buf_size);
//   while(1)
//   {
//       //std::cout<<"read_packet available"<<io->available() <<std::endl;
//       if(io->available() >= buf_size){
//           return io->read(buf, buf_size);
//       }
//       else{
//           std::this_thread::yield();
//       }
//   }
}

int DeviceIO::write_packet(void *opaque, uint8_t *buf, int buf_size)
{
    return 0;
}

int DeviceIO::blocking_read(uint8_t *buf, int buf_size)
{
    std::unique_lock<std::mutex> lock(mutex_);
    promisedlen = buf_size;
    cond_.wait(lock, [this, buf_size]{ return buf_size <=  (write_pos.load(std::memory_order_acquire) - read_pos );});

    DLOG(INFO)<<"blocking_read promisedlen: "<<promisedlen;
    return read(buf, buf_size);
}

int DeviceIO::blocking_write(uint8_t *buf, int buf_size)
{
    std::lock_guard<std::mutex> lock(mutex_);
    int len = write(buf, buf_size);
    if(available() >= promisedlen)
    {
        cond_.notify_one();
    }
    return len;
}



unsigned int DeviceIO::read(uint8_t *buf, unsigned int len)
{
    //unsigned int originlen = len;
    //originlen = len;


   //{
    //std::unique_lock<std::mutex> lock(mutex_);
    //cond_.wait(lock, [this, len]{ return len <=  (write_pos.load(std::memory_order_acquire) - read_pos );});
    //}

    len = std::min(len, write_pos.load(std::memory_order_acquire) - read_pos);


    unsigned int l = std::min(len, buffer_size - (read_pos & (buffer_size -1)));

    memcpy(buf, buffer + (read_pos & (buffer_size -1)), l);

    memcpy(buf+ l, buffer, len -l);

    //std::cout<<"read: " << len<<"---"<<originlen <<std::endl;
    //read_pos += len;
    read_pos.fetch_add(len, std::memory_order_release);
    return len;
}

unsigned int DeviceIO::write(uint8_t *buf, unsigned int len)
{
    //std::lock_guard<std::mutex> lock(mutex_);

    //memory_order_acquire保证在这个操作之后的memory accesses不会重排到这个操作之前去，但是这个操作之前的memory accesses可能会重排到这个操作之后去
    len = std::min(len, buffer_size -  (write_pos - read_pos.load(std::memory_order_acquire)));

    unsigned int l = std::min(len, buffer_size - (write_pos & (buffer_size - 1)));
    memcpy(buffer+ (write_pos & (buffer_size - 1)), buf, l);


    memcpy(buffer, buf + l, len -l );

    //write_pos += len;
    //memory_order_release保证在这个操作之前的memory accesses不会重排到这个操作之后去，但是这个操作之后的memory accesses可能会重排到这个操作之前去
    write_pos.fetch_add(len, std::memory_order_release);
    //LOG(INFO)<<"write: " << len <<"--current length : "<<write_pos - read_pos.load(std::memory_order_acquire);
    //if(write_pos - read_pos.load(std::memory_order_acquire) >= 4096)
        //cond_.notify_one();
    return len;
}

unsigned int DeviceIO::available()
{
    return write_pos - read_pos;
}

unsigned int DeviceIO::avio_rb8()
{
    unsigned char buf;
    read(&buf, 1);
    return (unsigned int)buf;
}

unsigned int DeviceIO::avio_rb16()
{
    unsigned int val;
    val = avio_rb8() << 8;
    val |= avio_rb8();
    return val;
}

unsigned int DeviceIO::avio_rb24()
{
    unsigned int val;
    val = avio_rb16() << 8;
    val |= avio_rb8();
    return val;
}

unsigned int DeviceIO::avio_rb32()
{
    unsigned int val;
    val = avio_rb16() << 8;
    val |= avio_rb16();
    return val;
}


Buffer::Buffer(int len):
    buffer(new unsigned char[len]), buffer_size(len), eof_reached(false), mutex_()
{
    //buf_ptr = buffer;
    buf_ptr = buf_end = buffer + buffer_size;
    LOG(INFO)<<"***********construct Buffer***"<<(uint64_t)buf_ptr<<std::endl;
}

int Buffer::avio_read(unsigned char *buf, int size)
{
    LOG(INFO)<<"avio_read: "<< size;
    if(buf_end - buf_ptr < size){
        fill_buffer();
    }

    memcpy(buf, buf_ptr, size);
    buf_ptr += size;
    return size;

}


void Buffer::fill_buffer()
{
//    if(buf_end <= buf_ptr){
//        buf_ptr = buffer;
//    }

    //buf_ptr = buffer;
    int len = buf_end - buf_ptr;
    if(len >= 0){

       memmove(buffer, buf_ptr, len);

    }


    len = io_->blocking_read(buffer + len, buffer_size - len);

    LOG(INFO)<<"fill_buffer read len: "<<len;//<< "  need len: " << len;
    buf_ptr = buffer;

    LOG(INFO)<<"fill_buffer";
    eof_reached = false;

}

int Buffer::read_packet(void *opaque, uint8_t *buf, int buf_size)
{
return 0;
}

int Buffer::write_packet(void *opaque, uint8_t *buf, int buf_size)
{
return 0;
}
