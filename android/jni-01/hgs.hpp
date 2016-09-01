#ifndef HGS_HPP__
#define HGS_HPP__
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <algorithm>

#if defined(__ANDROID__)
#  include <jni.h>
#  include <android/native_window_jni.h>
#  define NOT_PRINT_PROTO 1

//int     hgs_JNI_OnLoad(JavaVM* jvm, void*);
//JNIEnv* hgs_AttachCurrentThread();
//void    hgs_DetachCurrentThread();

//void* hgs_init_decoder(int w, int h, void* surface);
bool hgs_init( jint w, jint h, ANativeWindow* window);
#endif

int hgs_start(char const* ip, int port, char const* path);
void hgs_stop();

struct VideoFrameDecoded
{
    static VideoFrameDecoded query() { return VideoFrameDecoded(1); }

    void* data;
    unsigned size;
    bool empty() const { return oidx_ < 0; }

    VideoFrameDecoded() { oidx_ = -1; }
    ~VideoFrameDecoded() { _release(); }

    VideoFrameDecoded(VideoFrameDecoded && rhs) {
        data = rhs.data;
        size = rhs.size;
        oidx_ = rhs.oidx_;
        rhs.oidx_ = -1;
    }
    VideoFrameDecoded& operator=(VideoFrameDecoded && rhs) {
        if (this != &rhs) {
            this->_release();
            data = rhs.data;
            size = rhs.size;
            oidx_ = rhs.oidx_;
            rhs.oidx_ = -1;
        }
        return *this;
    }

private:
    VideoFrameDecoded(int) {
        oidx_ = _query(&data, &size);
    }
    int oidx_;
    void _release() {
        if (oidx_ >= 0) {
            _release(oidx_);
        }
    }
    static int  _query(void**data, unsigned* size);
    static void _release(int idx);
    VideoFrameDecoded(VideoFrameDecoded const&); // = delete;
    VideoFrameDecoded& operator=(VideoFrameDecoded const&); // = delete;
};


////////////////////////////////////// //////////////////////////////////////
//
//
#if !defined(NOT_PRINT_PROTO)
inline char* xsfmt(char xs[],unsigned siz, uint8_t*data,uint8_t*end)
{
    int len=std::min(16,int(end-data));
    for (int j=0, i=0; j < (int)siz && i < len; ++i)
        j += snprintf(&xs[j],siz-j, ((i>1&&i%2==0)?" %02x":"%02x"), (int)data[i]);
    return xs;
}
#endif

struct rtp_header
{
    uint8_t cc:4;         // CSRC count
    uint8_t x:1;          // header extension flag
    uint8_t p:1;          // padding flag
    uint8_t version:2;    // protocol version

    uint8_t pt:7;         // payload type
    uint8_t m:1;          // marker bit
    uint16_t seq;         // sequence number, network order

    uint32_t timestamp;     // timestamp, network order
    uint32_t ssrc;          // synchronization source, network order
    uint32_t csrc[1];        // optional CSRC list

    static rtp_header* cast(uint8_t* data, uint8_t* end) {
        if (data + sizeof(rtp_header)-4 > end)
            return nullptr;
        rtp_header* h = reinterpret_cast<rtp_header*>(data);
        if (data + h->length() > end)
            return nullptr;
        h->seq = ntohs(h->seq);
        h->timestamp = ntohl(h->timestamp) / 10; //XXX:  millseconds
        h->ssrc = ntohl(h->ssrc);
        for (unsigned i=0; i < h->cc; ++i)
            h->csrc[i] = ntohl(h->csrc[i]);
        return h;
    }
    unsigned length() const { return sizeof(rtp_header)-4 + 4*this->cc; }

    void print(uint8_t* data, uint8_t* end) {
#if !defined(NOT_PRINT_PROTO)
        char xs[128] = {};
        printf("%4u:%u: version %d p %d x %d cc %d pt %d seq %d: %s\n"
                , int(end-data), this->length()
                , this->version, this->p, this->x, this->cc, this->pt, this->seq
                , xsfmt(xs,sizeof(xs), data,end));
#endif
    }
};

struct nal_unit_header
{
    uint8_t type:5;
    uint8_t nri:2;
    uint8_t f:1;

    static nal_unit_header* cast(uint8_t* data, uint8_t* end) {
        if (data+1 > end)
            return nullptr;
        return reinterpret_cast<nal_unit_header*>(data);
    }
    unsigned length() const { return 1; }

    void print(uint8_t* data, uint8_t* end) {
#if !defined(NOT_PRINT_PROTO)
        char xs[128] = {};
        printf("%4u:%u: f %d nri %d type %d: %s\n"
                , int(end-data), this->length()
                , this->f, this->nri, this->type
                , xsfmt(xs,sizeof(xs), data,end));
#endif
    }
};

struct mbuffer /*: boost::intrusive::slist_base_hook<>*/
{
    struct rtpnalu_t {
        rtp_header rtp_h;
        uint32_t _4bytes;
        nal_unit_header nal_h; // size 0 pos
        uint8_t data_p[1];
    };

    ~mbuffer() {
        if (base_ptr_)
            free(base_ptr_);
    }
    mbuffer() {
        size_ = capacity_ = 0;
        base_ptr_ = 0; // rtp_h = rtp_header{};
    }
    mbuffer(rtp_header const& rh, nal_unit_header const& nh) {
        init(rh, nh);
    }
    mbuffer(rtp_header const& rh, nal_unit_header const* nal_begin, uint8_t const* end) {
        init(rh, *nal_begin);
        put( (uint8_t*)(nal_begin+1), end);
    }

    void put(uint8_t const* p, uint8_t const* end) {
        assert( base_ptr_ );
        unsigned siz = end - p;
        if (capacity_ - size_ < siz) {
            capacity_ += (siz+2047)/1024*1024;
            base_ptr_ = (rtpnalu_t*)realloc(base_ptr_, capacity_);
        }
        memcpy(this->end(), p, siz);
        size_ += siz;
    }

public:
    //struct rtp_header* rtp_header() const { return &base_ptr_->rtp_h; }
    nal_unit_header* nal_header() const { return &base_ptr_->nal_h; }

    uint8_t* begin_startbytes() const { return (uint8_t*)(&base_ptr_->_4bytes); }
    uint8_t* begin() const { return (uint8_t*)(&base_ptr_->nal_h); }
    uint8_t* data() const { return base_ptr_->data_p; }
    uint8_t* end() const { return (uint8_t*)base_ptr_ + size_; }

    bool empty() const { return size_==0; }

    rtpnalu_t* release() {
        rtpnalu_t* ptr = base_ptr_;
        base_ptr_ = 0;
        size_ = capacity_ = 0;
        return ptr;
    }
public: // movable-only
    mbuffer(mbuffer&& rhs) {
        memcpy(this, &rhs, sizeof(*this));
        rhs.size_ = rhs.capacity_ = 0;
        rhs.base_ptr_ = 0;
    }
    mbuffer& operator=(mbuffer&& rhs) {
        if (this != &rhs) {
            if (base_ptr_)
                free(base_ptr_);
            memcpy(this, &rhs, sizeof(*this));
            rhs.size_ = rhs.capacity_ = 0;
            rhs.base_ptr_ = 0;
        }
        return *this;
    }
private:
    rtpnalu_t *base_ptr_ = 0;
    unsigned size_, capacity_; // size_ includes nal_h
    void init(rtp_header const& rh, nal_unit_header const& nh) {
        capacity_ = 2048;
        base_ptr_ = (rtpnalu_t*)malloc(capacity_);
        base_ptr_->rtp_h = rh;
        base_ptr_->_4bytes = htonl(0x00000001); // base_ptr_->nal_h = nh;
        base_ptr_->nal_h = nh;
        size_ = sizeof(rtp_header)+4+sizeof(nal_unit_header);
    }
    mbuffer(mbuffer const&);// = delete;
    mbuffer& operator=(mbuffer const&);// = delete;
};

#include <functional>

void hgsnwk_init(char const* ip, int port, char const* path);
void hgsnwk_run(std::function<void(mbuffer)> sink);
void hgsnwk_exit(int);

#endif // HGS_HPP__

