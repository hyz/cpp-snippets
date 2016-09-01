#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <semaphore.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h> // #include <mutex>
//#include <set>
//#include <memory>
//#include <type_traits>
//#include <list>
#include <array>
#include <list>
#include <vector>
#include <algorithm>
#include <boost/noncopyable.hpp>
#include <boost/assert.hpp>
#include <boost/chrono/process_cpu_clocks.hpp>
// #include <initializer_list>

#include <OMX_Core.h>
#include <OMX_Component.h>
#include <OMX_Types.h>

#include "ildec.h"
#include "thread.hpp"
#include "clock.hpp"
#include "Include/Global.h"
#include "OMXMaster.hpp"
//#include "frameworks/av/media/libstagefright/omx/OMXMaster.h"
//#include <utils/Debug.h>
//#include <utils/Mutex.h>

//#define COMPONENT_NAME "OMX.Nvidia.h264.decode"
//#define COMPONENT_NAME "OMX.google.h264.decoder"
#define COMPONENT_NAME "OMX.MTK.VIDEO.DECODER.AVC"

#undef   LOG_TAG
#define  LOG_TAG    "HGSDEC"
#include "alog.hpp"

namespace android {
template<class T> static T* InitOMXParams(T *params)
{
    //COMPILE_TIME_ASSERT_FUNCTION_SCOPE(sizeof(OMX_PTR) == 4); // check OMX_PTR is 4 bytes.
    memset(params, 0, sizeof(T));
    params->nSize = sizeof(T);
    params->nVersion.s.nVersionMajor = 1;
    params->nVersion.s.nVersionMinor = 0;
    params->nVersion.s.nRevision = 0;
    params->nVersion.s.nStep = 0;

    return params; //params->nPortIndex = portIndex;
}
} // namespace android

struct nal_unit_header
{
    uint8_t type:5;
    uint8_t nri:2;
    uint8_t f:1;
};

#if 0
struct h264nalu_reader
{
    typedef std::array<uint8_t*,2> range;
    uint8_t *begin_ = 0, *end_;

    bool open(char const* h264_filename)
    {
        int fd = ::open(h264_filename, O_RDONLY);
        if (fd >= 0) {
            struct stat st; // fd = open(fn, O_RDONLY);
            fstat(fd, &st); // LOGD("Size: %d", (int)st.st_size);

            void* p = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
                void* m = malloc(st.st_size);
                memcpy(m, p, st.st_size);
                begin_ = (uint8_t*)m;
                end_ = begin_ + st.st_size;
            munmap(p, st.st_size);
            assert(begin_[0]=='\0'&& begin_[1]=='\0'&& begin_[2]=='\0'&& begin_[3]==1);

            close(fd);
        }
        ERR_EXIT_IF(begin_==0, "%s", h264_filename);
        return begin_!=0; //std::make_pair(begin_,begin_);
    }

    range next(range r, int type=0) const {
        for (r = find_(r[1]); r[0] != r[1]; r = find_(r[1])) {
            nal_unit_header* h = reinterpret_cast<nal_unit_header*>(r[0]+4);
            if (h->nri < 3) {
                continue;
            }
            if (type && h->type != type) {
                LOGD("nal nri %d type %d", int(h->nri), int(h->type));
                continue;
            }
            LOGD("nal nri %d type %d, len %u", int(h->nri), int(h->type), int(r[1]-r[0]));
            break;
        }
        return r; //find_(begin_);
    }
    range begin(int naltype=0) const {
        return next(range{{begin_,begin_}}, naltype);
    }

    ~h264nalu_reader() {
        if (begin_) free(begin_);
    }
    range find_(uint8_t* e) const {
        uint8_t* b = e;
        if (e+4 < end_) {
            static uint8_t startbytes[] = {0, 0, 0, 1};
            //b = e + 4;
            e = std::search(e+4, end_, &startbytes[0], &startbytes[4]);
        }
        return range{{b,e}};
    }
};
#endif

typedef clock_realtime_type Clock;

struct Timepx {
    Clock::time_point empty, callback; //, polled;
};
//static Timepx* sTimepx() { static Timepx x; return &x;}

static struct StatisTest {
    unsigned seq_ = 0;
    //int nPoll = 0;
    //int nFillCB = 0;
    //unsigned nEmpty = 0, nEmptyCB = 0;
    //int maxPoll = 0;
} gTst;

template <bool> struct FlagsHlp;
template <> struct FlagsHlp<true> {
    template <typename Int> static void set(Int&v, unsigned m) { v |= m; }
};
template <> struct FlagsHlp<false> {
    template <typename Int> static void set(Int&v, unsigned m) { v &= ~m; }
};

struct ILAvcDec : boost::noncopyable
{
    typedef ILAvcDec ThisType;
    //std::list<Timepx> timepx;
    //void timepx_new(Timepx*& i, Timepx*& j) { j = i = &*timepx.emplace(timepx.end()); }

    struct BufferEx {
        enum { MSK_USED = 0x04, MSK_COPYED = 0x10 };

        OMX_BUFFERHEADERTYPE* buf;
        unsigned char flags;
        unsigned char indexTestOnly;
        Timepx ti; // Test-only //TODO //std::vector<BufferEx>::iterator reli;

        BufferEx(OMX_BUFFERHEADERTYPE* b=0) : buf(b) {
            flags=0;
        }

        bool used() const { return (flags & MSK_USED); }
        template <bool y> void used() { FlagsHlp<y>::set(flags, MSK_USED); }
        bool copyed() const { return (flags & MSK_COPYED); }
        template <bool y> void copyed() { FlagsHlp<y>::set(flags, MSK_COPYED); }
    };

    pthread_mutex_type mutex_;
    pthread_cond_type cond_;

    struct IOPort : OMX_PARAM_PORTDEFINITIONTYPE  {
        std::vector<BufferEx> bufs_;
        ThisType* thiz;
        unsigned count;

        IOPort(ThisType* p, unsigned char portIdx) /*: std::vector<BufferWrap>(24)*/ {
            thiz = p;
            nPortIndex = portIdx;
            count = 0;
        }
        //~IOPort() { omxbufs_deinit(component_); }

        OMX_HANDLETYPE get_handle() { return thiz->component_; }

        std::vector<BufferEx>& buffers() const { return const_cast<IOPort*>(this)->bufs_; }
        typedef std::vector<BufferEx>::iterator iterator;

        iterator begin() const { return const_cast<IOPort*>(this)->bufs_.begin(); }
        iterator end() const { return const_cast<IOPort*>(this)->bufs_.end(); }
        BufferEx& front() { return bufs_.front(); }
        BufferEx& back() { return bufs_.back(); }
        void resize(unsigned n) { return bufs_.resize(n); }
        void clear() { return bufs_.clear(); }
        bool empty() const { return bufs_.empty(); }
        unsigned size() const { return bufs_.size(); }
        //void reserve(unsigned n) { return bufs_.reserve(n); }

        int index(iterator it) const { return int(it - bufs_.begin()); }
        iterator iterator_to(BufferEx& b) {
            //int x = (&b - &front());
            //LOGD("P%d %d=%d %p", nPortIndex, x, int(b.indexTestOnly), &b);
            return bufs_.begin() + (&b - &front());
        }

        int countf(unsigned msk) const {
            int n = 0;
            for (BufferEx const& b : buffers()) {
                if (b.flags & msk)
                    ++n;
                //LOGD("%p", &b);
            }
            return n;
        }

        iterator FindFreeBuffer() {
            //LOGD("P%d size %u", nPortIndex, size());
            for (auto beg = begin(); beg != end(); ++beg) {
                if (!beg->used()) {
                    return beg; //->buf; //OMX_BUFFERHEADERTYPE* bh = b.buf; //buf(i);
                }
            }
            return end();
        }

        void echo() const {
            OMX_PARAM_PORTDEFINITIONTYPE const& def = *this;
            OMX_VIDEO_PORTDEFINITIONTYPE const& vdef = def.format.video;
            LOGD("Port %d nBufferCount(Actual/Min) %d %d, %.1fK, Enabled %d, Populated %d"
                        "\n\t%s, %ux%u %u %u, xFrate %u, nBrate %u, color %u, compress %u"
                    , def.nPortIndex, def.nBufferCountActual,def.nBufferCountMin, def.nBufferSize/1024.0, def.bEnabled, def.bPopulated
                    , vdef.cMIMEType
                    , vdef.nFrameWidth, vdef.nFrameHeight, vdef.nStride, vdef.nSliceHeight
                    , vdef.xFramerate, vdef.nBitrate
                    , vdef.eColorFormat, vdef.eCompressionFormat //OMX_COLOR_FormatYUV420Planar=19,OMX_VIDEO_CodingAVC=7
                    );
        }
    };
    typedef IOPort::iterator iterator;
    typedef std::array<uint8_t*,2> range;

    struct InputPort : IOPort {
        unsigned nReplaced = 0;
        InputPort(ThisType* p) : IOPort(p, 0) {
        }
        
        void send(uint8_t* data, uint8_t* end, int flags=0) {
            iterator i = begin();
            iterator j = begin() + (size()+1)/2;
            bool doempty = 0;
            //pthread_mutex_lock_guard lk(thiz->mutex_);

            if (!i->used() && !j->used()) {
                doempty=1;
            } else if (!i->used()) {
            } else if (!j->used()) {
                i = j;
            } else {
                ERR_EXIT("both used");
            }

            if (i->copyed())
                ++nReplaced;

            copydata(i, data, end, OMX_BUFFERFLAG_ENDOFFRAME|OMX_BUFFERFLAG_DECODEONLY|flags);
            if (doempty) {
                //thiz->timepx_new(i->ti, thiz->output_port.curp_->ti);
                EmptyThisBuffer( i );
            }
        }
        void callback(iterator it) {
            //gTst.nEmptyCB++;

            pthread_mutex_lock_guard lk(thiz->mutex_);
            ++count;
            it->ti.callback = Clock::now();
            LOGD("mills %u replaced %u", milliseconds(it->ti.callback - it->ti.empty), nReplaced);
            //LOGD("from-middle-night %u %p", milliseconds(it->ti->callback - Clock::middle_night()), it->ti);

            thiz->cond_.signal();

            it->flags = 0; //&= ~MSK_USED;

            iterator j = (it == begin() ? begin() + (size()+1)/2 : begin());
            if (j->copyed()) {
                //thiz->timepx_new(j->ti, thiz->output_port.curp_->ti);
                EmptyThisBuffer(j);
            }
        }
        void EmptyThisBuffer(iterator it) {
            // OMX_BUFFERFLAG_CODECCONFIG OMX_BUFFERFLAG_ENDOFFRAME OMX_BUFFERFLAG_DECODEONLY
            it->used<true>(); //it->flags |= MSK_USED;
            it->ti.empty = Clock::now();
            nReplaced = 0;
            OMX_ERRORTYPE err = OMX_EmptyThisBuffer(get_handle(), it->buf);
            ERR_EXIT_IF(err!=OMX_ErrorNone, "OMX_EmptyThisBuffer %d", int(err));
            LOGD("P0[%d] len %u", INDEX(it), it->buf->nFilledLen);
        }

        static iterator copydata(iterator it, uint8_t const* data, uint8_t const* end, int flags) {
            OMX_BUFFERHEADERTYPE* bh = it->buf;
            bh->nFlags = flags;
            bh->nOffset = 0;
            bh->nFilledLen = end - data;
            memcpy(bh->pBuffer, data, bh->nFilledLen);
            it->copyed<true>(); // it->flags |= BufferEx::MSK_COPYED;
            return it;
        }

        iterator EmptyThis(uint8_t const* beg, uint8_t const* end, int flags) {
            auto it = FindFreeBuffer();
            if (it == this->end()) {
                ERR_EXIT("None");
            } else {
                EmptyThisBuffer( copydata(it, beg, end, flags) );
            }
            return it;
        }
    } input_port;

    struct OutputPort : IOPort {
        //OMX_BUFFERHEADERTYPE last;
        iterator curp_;

        OutputPort(ThisType* p) : IOPort(p, 1) {
            //memset(&last, 0, sizeof(OMX_BUFFERHEADERTYPE));
        }

        void ready() {
            curp_ = begin();
        }

        void callback(iterator it) {
            //{
            //    pthread_mutex_lock_guard lk(mutex_);
            //    last = *it->buf;
            //    cond_.signal();
            //}
            //gTst.nFillCB++;
            FillThisBuffer(it);
            ////it->flags &= ~MSK_USED;
            ///oseq_.push_back(it);
        }
        Image* poll(Image& img, bool* interrupt)
        {
            iterator it = curp_;
            if (it->buf->nFilledLen <= 0) {
                return 0;
            }
            if (++curp_ == this->end()) {
                curp_ = this->begin();
            }

            //int nP=0;
            //while (buf->nFilledLen <= 0) {
            //    if (++nP > 30) {
            //        LOGD("poll: P1[%d] %d", INDEX(it), nP);
            //        usleep(100000);
            //        nP=0;
            //    }
            //    usleep(3000);
            //    if (*interrupt) {
            //        return 0;
            //    }
            //}
            auto* buf = it->buf;
            OMX_VIDEO_PORTDEFINITIONTYPE const& v = format.video;
            img.width = v.nFrameWidth;
            img.height = v.nFrameHeight;
            img.stride = v.nStride;
            img.size = buf->nFilledLen;
            img.pdata = img.porg = buf->pBuffer + buf->nOffset;
            LOGD("P1[%d] %d", INDEX(it), buf->nFilledLen);

            ++count;
            //out[0] = buf->pBuffer + buf->nOffset;
            //out[1] = out[0] + buf->nFilledLen;
            ////img = *buf; //OMX_BUFFERHEADERTYPE img = *it->buf;

            //if (gTst.maxPoll < nP) { gTst.maxPoll = nP; }
            //gTst.nPoll++;
            //auto tp = it->ti; it->ti = sTimepx();
            //tp->polled = Clock::now();

            //appendfile("/sdcard/o.yuv", buf->pBuffer, 1920*1080);

            /// pthread_mutex_lock_guard lk(mutex_);
            /// if (last.nFilledLen == 0) {
            ///     cond_.wait(mutex_);
            /// }
            /// out[0] = last.pBuffer + last.nOffset;
            /// out[1] = out[0] + last.nFilledLen;
            /// if (img) {
            ///     *img = last;
            /// }
            /// last.nFilledLen = 0;
            return &img; //unsigned(out[1] - out[0]); //(void*)&out[0];
        }

        void FillThisBuffer(iterator it) {
            OMX_BUFFERHEADERTYPE* bh = it->buf;
            bh->nFlags = 0;
            bh->nOffset = 0;
            bh->nFilledLen = 0;
                //enum { Y0=1920*1080 };
                //static char z16[16] = {0};
                //memcpy(bh->pBuffer+Y0, z16, 16);
                //memcmp(bh->pBuffer+Y0, z16, 16);
            it->used<true>(); //it->flags |= MSK_USED;
            LOGD("P1[%d] %d %d %x", INDEX(it), bh->nFilledLen, bh->nOffset, bh->nFlags);
            OMX_ERRORTYPE err = OMX_FillThisBuffer(get_handle(), bh);
            ERR_EXIT_IF(err!=OMX_ErrorNone, "OMX_FillThisBuffer %d", int(err));
        }
        iterator FillOneBuffer() {
            auto it = FindFreeBuffer();
            if (it != this->end()) {
                FillThisBuffer(it);
            }
            return it;
        }

    } output_port;

    android::OMXMaster omxMaster; // err = OMX_Init();
    OMX_HANDLETYPE component_; //OMX_COMPONENTTYPE *component_;
    OMX_STATETYPE state;
    unsigned char stage_ = 0;

    sem_t sem_command_;
    sem_t sem_bufempty_;
    //sem_t sem_buffilled_;
    //std::list<iterator> oseq_;
    //android::Mutex mMutex;

    ILAvcDec(char const* component_name)
        : input_port(this), output_port(this)
    {
        //static_assert(std::is_same<OMX_HANDLETYPE,OMX_COMPONENTTYPE*>::value);
        static OMX_CALLBACKTYPE callbacks = {
            .EventHandler = &EventHandler0,
            .EmptyBufferDone = &EmptyBufferDone0,
            .FillBufferDone = &FillBufferDone0
        };
        sem_init(&sem_command_, 0, 0);
        sem_init(&sem_bufempty_, 0, 0);
        //sem_init(&sem_buffilled_, 0, 0);

        OMX_COMPONENTTYPE *handle;
        OMX_ERRORTYPE err = omxMaster.makeComponentInstance(component_name, &callbacks, this, &handle);
        ERR_MSG_IF(err!=OMX_ErrorNone, "makeComponentInstance");
        component_ = /*(OMX_HANDLETYPE)*/handle; // OMX_GetHandle
        LOGD("%p %p %s, %p %p", this, component_, component_name, &input_port, &output_port);
    }
    ~ILAvcDec() {
        if (component_)
            omxMaster.destroyComponentInstance((OMX_COMPONENTTYPE*)component_);
        sem_destroy(&sem_command_);
        sem_destroy(&sem_bufempty_);
        //sem_destroy(&sem_buffilled_);
        LOGD("%p", this);
    }

    void teardown() {
        LOGD("===");
        if (stage_ < 2)
            ;
        stage_ = 3;
        while (sem_trywait(&sem_bufempty_) == 0)
            ;
        assert(errno == EAGAIN);
        while (sem_trywait(&sem_command_) == 0)
            ;
        assert(errno == EAGAIN);

        command(OMX_CommandFlush, output_port.nPortIndex, NULL).wait();
        command(OMX_CommandStateSet, OMX_StateIdle, NULL).wait();
        command(OMX_CommandStateSet, OMX_StateLoaded, NULL)([this](){
                omxbufs_deinit(input_port);
                omxbufs_deinit(output_port);
            }).wait();
    }

    bool setup(unsigned vWidth, unsigned vHeight, uint8_t* sps[2], uint8_t* pps[2])
    {
        //// OMX_IndexParamVideoInit, OMX_PORT_PARAM_TYPE // OMX_VIDEO_PARAM_PROFILELEVELTYPE

        sync_state();
        {
            OMX_PARAM_COMPONENTROLETYPE roleParams;
            android::InitOMXParams(&roleParams);
            strncpy((char *)roleParams.cRole, "video_decoder.avc", OMX_MAX_STRINGNAME_SIZE - 1);
            roleParams.cRole[OMX_MAX_STRINGNAME_SIZE - 1] = '\0';
            OMX_ERRORTYPE err = OMX_SetParameter(component_, OMX_IndexParamStandardComponentRole, &roleParams);
            ERR_EXIT_IF(err!=OMX_ErrorNone, "OMX_SetParameter OMX_IndexParamStandardComponentRole");
        } {
            OMX_VIDEO_PARAM_PORTFORMATTYPE format;
            android::InitOMXParams(&format)->nPortIndex = 0; //kPortIndexInput;
            format.nIndex = 0;
            for ( ;; ) {
                OMX_ERRORTYPE err = OMX_GetParameter( component_, OMX_IndexParamVideoPortFormat, &format);
                ERR_EXIT_IF(err!=OMX_ErrorNone, "OMX_GetParameter OMX_IndexParamVideoPortFormat");
                if (format.eCompressionFormat == OMX_VIDEO_CodingAVC && format.eColorFormat == OMX_COLOR_FormatUnused) {
                    OMX_SetParameter(component_, OMX_IndexParamVideoPortFormat, &format);
                    break;
                }
                format.nIndex++;
            }
        } {
            sync_port_definition(input_port);
            OMX_PARAM_PORTDEFINITIONTYPE def = input_port;
            def.nBufferSize = vWidth*vHeight*3/2;
            def.format.video.nFrameWidth = vWidth;
            def.format.video.nFrameHeight = vHeight;
            def.format.video.eCompressionFormat = OMX_VIDEO_CodingAVC;
            def.format.video.eColorFormat = OMX_COLOR_FormatUnused;
            //def.format.video.nStride = def.format.video.nFrameWidth;
            sync_port_definition(input_port, def);
        } /*{
            OMX_PARAM_PORTDEFINITIONTYPE def = output_port;
            def.format.video.nFrameWidth = vWidth;
            def.format.video.nFrameHeight = vHeight;
            def.format.video.nStride = def.format.video.nFrameWidth;
            sync_port_definition(output_port, def);
        }*/// init_port_def(output_port); //(OMX_DirOutput);

        //enable_nativebufs();
        sync_port_definition(output_port);

        command(OMX_CommandStateSet, OMX_StateIdle, NULL)([this](){
                    omxbufs_reinit(input_port);
                    omxbufs_reinit(output_port);
                }).wait();
        sync_state();
        command(OMX_CommandStateSet, OMX_StateExecuting, NULL).wait();
        sync_state();
        sync_port_definition(input_port);
        sync_port_definition(output_port);

        for (int i=0; i<5; ++i)
            output_port.FillOneBuffer();

        {
#if 0
            static unsigned char sps_[] = { 0,0,0,1,
                0x67, 0x64, 0x00, 0x28, 0xac, 0xf0, 0x1e, 0x00, 0x89, 0xf9, 0x50
              //0x67, 0x64, 0x00, 0x28, 0xac, 0xe8, 0x07, 0x80, 0x22, 0x7e, 0x58, 0x02
            };
            sps[0] = sps_;
            sps[1] = &sps_[sizeof(sps_)];
#endif
            input_port.EmptyThis(sps[0], sps[1], OMX_BUFFERFLAG_CODECCONFIG|OMX_BUFFERFLAG_ENDOFFRAME);
        } {
            input_port.EmptyThis(pps[0], pps[1], OMX_BUFFERFLAG_CODECCONFIG|OMX_BUFFERFLAG_ENDOFFRAME);
        }

        CommandHelper{this,OMX_CommandMax,OMX_EventPortSettingsChanged,0}.wait();
        LOGD("=== OMX_EventPortSettingsChanged ...");

        sync_port_definition(output_port);
        command(OMX_CommandPortDisable, output_port.nPortIndex, NULL)([this](){
                    LOGD("=== OMX_EventBufferFlag P1 cntf %d ...", output_port.countf(BufferEx::MSK_USED));
                    if (output_port.countf(BufferEx::MSK_USED)>0)
                        CommandHelper{this,OMX_CommandMax,OMX_EventBufferFlag,0}.wait(); //XXX
                    omxbufs_deinit(output_port);
                }).wait();
        LOGD("=== OMX_CommandPortDisable OK");

        sync_port_definition(output_port);
        command(OMX_CommandPortEnable, output_port.nPortIndex, NULL)([this](){
                    omxbufs_reinit(output_port);
                }).wait();
        LOGD("=== OMX_CommandPortEnable OK");//sleep(1);

        sync_port_definition(input_port);
        sync_port_definition(output_port); //sync_state();

        while (sem_trywait(&sem_bufempty_) == 0)
            LOGD("sem_trywait(&sem_bufempty_) == 0");
        assert(errno == EAGAIN);
        while (sem_trywait(&sem_command_) == 0)
            LOGD("sem_trywait(&sem_command_) == 0");
        assert(errno == EAGAIN);

        stage_ = 1;
        LOGD("=== stage 1"); //sleep(1);
        for (int x=2; x>0; --x) {
            output_port.FillOneBuffer();
            sem_wait(&sem_command_);
        }
        sem_wait(&sem_bufempty_);
        sem_wait(&sem_bufempty_);
        while (sem_trywait(&sem_command_) == 0)
            ;
        while (sem_trywait(&sem_bufempty_) == 0)
            ;

        stage_ = 2;
        for (auto beg = output_port.begin(); beg != output_port.end(); ++beg) {
            output_port.FillThisBuffer(beg);
            //oseq_.push_back(beg);
        }
        output_port.ready();
        output_port.count = input_port.count = 0;
        LOGD("=== stage 2"); //sleep(1);

        return true;
    }

    void input(uint8_t* data, uint8_t* end, int flags=0) {
        if (stage_ == 2) {
            input_port.send(data, end, flags);
        } else {
            ERR_MSG("stage %d", stage_);
        }
    }

    //unsigned outpoll(uint8_t* out[2], OMX_BUFFERHEADERTYPE* bh = 0)
    Image* outpoll(Image& img, bool* interrupt) //(uint8_t* out[2]/*, OMX_BUFFERHEADERTYPE* bh = 0*/);
    {
        int nW = 0;
        pthread_mutex_lock_guard lk(mutex_);
        while (output_port.count >= input_port.count && !*interrupt) {
            if (cond_.wait(mutex_, 5000) == ETIMEDOUT) {
                ++nW;
                if (nW > 3000/5) {
                    nW = 0;
                    LOGD("cond:wait ETIMEDOUT %d", nW);
                }
            }
        }
        if (*interrupt) {
            return 0;
        }
        return output_port.poll(img, interrupt);
    }

private:
    inline void appendfile(char const* fn, void* buf, unsigned len)
    {
        if (FILE* fp = fopen(fn, "a")) {
            fwrite(buf, len, 1, fp);
            fclose(fp);
        }
    }

    OMX_STATETYPE sync_state() {
        OMX_STATETYPE state0 = state;
        OMX_GetState(component_, &state);
        LOGD("state %d:%s => %d:%s", state0, state_str(state0), state, state_str(state));
        return state;
    }

    void sync_port_definition(IOPort& iop) {
        int portIndex = iop.nPortIndex;

        OMX_PARAM_PORTDEFINITIONTYPE* def = &iop;
        android::InitOMXParams(def)->nPortIndex = portIndex;
        OMX_ERRORTYPE err=OMX_GetParameter(component_, OMX_IndexParamPortDefinition, def);
        ERR_EXIT_IF(err!=OMX_ErrorNone, "OMX_GetParameter");

        iop.echo();
    }

    void sync_port_definition(IOPort& iop, OMX_PARAM_PORTDEFINITIONTYPE const& def) {
        OMX_ERRORTYPE err=OMX_SetParameter(component_
                , OMX_IndexParamPortDefinition, const_cast<OMX_PARAM_PORTDEFINITIONTYPE*>(&def));
        ERR_EXIT_IF(err!=OMX_ErrorNone, "OMX_SetParameter");
        sync_port_definition(iop);
    }

    void omxbufs_deinit(IOPort& iop) {
        LOGD("P%d: Actual %u, %d", iop.nPortIndex, iop.nBufferCountActual, (int)iop.size());
        for (auto beg = iop.begin(); beg != iop.end(); ++beg) {
            OMX_FreeBuffer(component_, iop.nPortIndex, beg->buf);
        }
        iop.clear();
    }
    void omxbufs_reinit(IOPort& iop)/*(OMX_HANDLETYPE component)*/ {
        if (!iop.empty())
            omxbufs_deinit(iop);
        LOGD("P%d: Actual %u", iop.nPortIndex, iop.nBufferCountActual);

        iop.resize(iop.nBufferCountActual);
        int idx = 0;
        for (BufferEx & b : iop.buffers()) {
            b = BufferEx();
            b.indexTestOnly = idx++; //(unsigned char)(beg - iop.begin());
            b.buf = omxbuf_alloc(&iop, &b, idx);
        LOGD("OMX_AllocateBuffer P%d[%d] %u, %p=%p %p", iop.nPortIndex, idx, iop.nBufferSize, &b, b.buf->pAppPrivate, b.buf);
        }
    }

    struct CommandHelper
    {
        ThisType* self;
        OMX_COMMANDTYPE Cmd; OMX_U32 nParam1; OMX_PTR pCmdData;

        CommandHelper const& send() const {
            LOGD("");
            if (Cmd < OMX_CommandMax) {
                OMX_ERRORTYPE err = OMX_SendCommand(self->component_, Cmd, nParam1, pCmdData);
                ERR_EXIT_IF(err!=OMX_ErrorNone, "OMX_SendCommand");
            }
            return *this;
        }
        template <typename F> CommandHelper const& operator()(F&& fn) const {
            fn();
            return *this;
        }
        void wait() const {
            echo("wait");
            int ret = sem_wait(&self->sem_command_); // OMX_StateExecuting OMX_StateWaitForResources
            ERR_EXIT_IF(ret<0, "sem_wait");
            if (OMX_CommandStateSet == nParam1)
                self->sync_state();
            echo("wait"," [OK]");
        }
        void echo(char const* pfx, char const* end="") const {
            if (Cmd == OMX_CommandStateSet)
                LOGD("%s:%s: %s%s", pfx, self->cmd_str(Cmd), self->state_str(nParam1), end);
            else if (Cmd == OMX_CommandMax)
                LOGD("%s:%s: %u%s", pfx, self->event_str(nParam1), nParam1, end);
            else
                LOGD("%s:%s: %u%s", pfx, self->cmd_str(Cmd), nParam1, end);
        }
    };
    CommandHelper command(OMX_COMMANDTYPE Cmd, OMX_U32 nParam1, OMX_PTR pCmdData) {
        return CommandHelper{this,Cmd,nParam1,pCmdData}.send();
    }

    //void enable_nativebufs()
    //{
    //    OMX_INDEXTYPE index;
    //    OMX_STRING name = const_cast<OMX_STRING>("OMX.google.android.index.useAndroidNativeBuffer");
    //    OMX_ERRORTYPE err = OMX_GetExtensionIndex(mHandle, name, &index);
    //    ERR_EXIT_IF(err != OMX_ErrorNone, "%s", name);

    //    //BufferMeta *bufferMeta = new BufferMeta(graphicBuffer);
    //    OMX_BUFFERHEADERTYPE *header;

    //    OMX_VERSIONTYPE ver;
    //    ver.s.nVersionMajor = 1;
    //    ver.s.nVersionMinor = 0;
    //    ver.s.nRevision = 0;
    //    ver.s.nStep = 0;
    //    UseAndroidNativeBufferParams params = {
    //        sizeof(UseAndroidNativeBufferParams), ver, portIndex, NULL,
    //        &header, graphicBuffer,
    //    };

    //    err = OMX_SetParameter(mHandle, index, &params);

    //    EnableAndroidNativeBuffersParams params;
    //    android::InitOMXParams(&params)->nPortIndex = output_port.nPortIndex;
    //    params.enable = 1;
    //    OMX_ERRORTYPE err = OMX_SetParameter(mHandle, index, &params);
    //    ERR_EXIT_IF(err!=OMX_ErrorNone, "OMX_SetParameter %d", int(err));
    //}

private:
    //static inline unsigned MASK(OMX_BUFFERHEADERTYPE* bh) { return (1<<INDEX(bh)); }
    static inline unsigned INDEX(iterator it) {
        return (it->indexTestOnly); //(it->buf->nPortIndex==0) ? input_port.index(it) : output_port.index(it);
        //return (unsigned(bh->pAppPrivate)&0x3f);
    }

    OMX_BUFFERHEADERTYPE* omxbuf_alloc(OMX_PARAM_PORTDEFINITIONTYPE* def, OMX_PTR ptr, int idx) {
        OMX_BUFFERHEADERTYPE* bh = 0;
        //OMX_PARAM_PORTDEFINITIONTYPE* def = &port;
        //if (bidx < def->nBufferCountActual && bidx < port.size()) {
        //BufferEx* ptr = &*it; //.operator->();
        OMX_ERRORTYPE err = OMX_AllocateBuffer(component_, &bh, def->nPortIndex, (OMX_PTR)ptr, def->nBufferSize); // pAppPrivate
        ERR_EXIT_IF(err!=OMX_ErrorNone, "OMX_AllocateBuffer P%d[%d] %u: %s", def->nPortIndex, idx, def->nBufferSize, error_str(err));
        //}
        //bh->nFlags = 0;
        //bh->nOffset = 0;
        //bh->nFilledLen = 0;
        return bh;
    }

private: // callbacks
    OMX_ERRORTYPE EmptyBufferDone(iterator it)
    {
        OMX_BUFFERHEADERTYPE* bh = it->buf;
        if (stage_ > 2)
            return OMX_ErrorNone;
        LOGD("P0[%d] %u %u %u, %d", INDEX(it), bh->nFilledLen, bh->nOffset, bh->nAllocLen, int(stage_));
    
        if (stage_ == 2) {
            input_port.callback(it);
        } else {
            it->flags = 0; //&= ~BufferEx::MSK_USED;
            sem_post(&sem_bufempty_);
        }
        return OMX_ErrorNone;
    }

    OMX_ERRORTYPE FillBufferDone(iterator it)
    {
        OMX_BUFFERHEADERTYPE* bh = it->buf;
        if (stage_ > 2)
            return OMX_ErrorNone;
        LOGD("P1[%d] %u %u %u, %d", INDEX(it), bh->nFilledLen, bh->nOffset, bh->nAllocLen, int(stage_));

        if (stage_ == 2) {
            output_port.callback(it);
        } else {
            it->flags = 0; // //it->used<false>();
            //LOGD("stage %d, %p", int(stage_), &*it);
            if (stage_==1 || (stage_==0 && output_port.countf(BufferEx::MSK_USED) == 0)) {
                LOGD("P1[%d] stage=%d", INDEX(it), int(stage_));
                sem_post(&sem_command_); // OMX_EventBufferFlag
            }
        }
        return OMX_ErrorNone;
    }

    OMX_ERRORTYPE cbEventHandler(OMX_OUT OMX_EVENTTYPE eEvent
            , OMX_OUT OMX_U32 Data1, OMX_OUT OMX_U32 Data2, OMX_OUT OMX_PTR pEventData)
    {
        LOGD("%u:%s %u %u %p", int(eEvent),event_str(eEvent), Data1, Data2, pEventData);

        switch (eEvent) {
            case OMX_EventCmdComplete:
                switch ((OMX_COMMANDTYPE)Data1) {
                    case OMX_CommandStateSet:
                        LOGD("OMX_EventCmdComplete:OMX_CommandStateSet %d:%s", (int)Data2,state_str(Data2));
                        sem_post(&sem_command_);
                        break;
                    case OMX_CommandPortDisable:
                        LOGD("OMX_EventCmdComplete:OMX_CommandPortDisable %d %d", (int)Data1, (int)Data2);
                        sem_post(&sem_command_);
                        break;
                    case OMX_CommandPortEnable:
                        LOGD("OMX_EventCmdComplete:OMX_CommandPortEnable %d %d", (int)Data1, (int)Data2);
                        sem_post(&sem_command_);
                        break;
                    case OMX_CommandFlush:
                        LOGD("OMX_EventCmdComplete:OMX_CommandFlush %d %d", (int)Data1, (int)Data2);
                        sem_post(&sem_command_);
                        break;
                }
                break;
            case OMX_EventPortSettingsChanged: // TODO
                LOGD("OMX_EventPortSettingsChanged %d %d", (int)Data1, (int)Data2);
                sem_post(&sem_command_);
                break;
            case OMX_EventBufferFlag: ///**< component has detected an EOS */
                ERR_EXIT("OMX_EventBufferFlag");
            case OMX_EventError: ///**< component has detected an error condition */
                ERR_EXIT("OMX_EventError");
        }
        return OMX_ErrorNone;
    }

private:
    static OMX_ERRORTYPE EventHandler0(OMX_OUT OMX_HANDLETYPE hComponent, OMX_OUT OMX_PTR pAppData
            , OMX_OUT OMX_EVENTTYPE eEvent, OMX_OUT OMX_U32 Data1, OMX_OUT OMX_U32 Data2, OMX_OUT OMX_PTR pEventData)
    {
        ERR_MSG_IF(hComponent!=static_cast<ThisType*>(pAppData)->component_, "%p %p", hComponent,pAppData);
        return static_cast<ThisType*>(pAppData)->cbEventHandler(eEvent, Data1, Data2, pEventData);
    }
    static OMX_ERRORTYPE EmptyBufferDone0(OMX_OUT OMX_HANDLETYPE hComponent, OMX_OUT OMX_PTR pAppData
            , OMX_OUT OMX_BUFFERHEADERTYPE* pBuffer)
    {
        ERR_MSG_IF(hComponent!=static_cast<ThisType*>(pAppData)->component_, "%p %p", hComponent,pAppData);
        assert(pBuffer);
        ThisType* thiz = static_cast<ThisType*>(pAppData);
        BufferEx* bx = static_cast<BufferEx*>(pBuffer->pAppPrivate);
        return thiz->EmptyBufferDone( thiz->input_port.iterator_to(*bx) );
    }
    static OMX_ERRORTYPE FillBufferDone0(OMX_OUT OMX_HANDLETYPE hComponent, OMX_OUT OMX_PTR pAppData
            , OMX_OUT OMX_BUFFERHEADERTYPE* pBuffer)
    {
        ERR_MSG_IF(hComponent!=static_cast<ThisType*>(pAppData)->component_, "%p %p", hComponent,pAppData);
        assert(pBuffer);
        //return static_cast<ThisType*>(pAppData)->FillBufferDone(*static_cast<BufferEx*>(pBuffer->pAppPrivate));//((unsigned)pBuffer->pAppPrivate);
        ThisType* thiz = static_cast<ThisType*>(pAppData);
        BufferEx* bx = static_cast<BufferEx*>(pBuffer->pAppPrivate);
        //BufferEx* b0 = &thiz->output_port.front();
        //LOGD("thiz %p b %p=%p P%d %d=%d", thiz, bx, b0, thiz->output_port.nPortIndex, (int)bx->indexTestOnly, int(bx-b0));
        return thiz->FillBufferDone( thiz->output_port.iterator_to(*bx) );
    }

#define CASE_THEN_RETURN_STR(y) case y: return (#y)
    static char const* cmd_str(unsigned x) {
        switch (x) {
            CASE_THEN_RETURN_STR(OMX_CommandStateSet);
            CASE_THEN_RETURN_STR(OMX_CommandFlush);
            CASE_THEN_RETURN_STR(OMX_CommandPortDisable);
            CASE_THEN_RETURN_STR(OMX_CommandPortEnable);
            CASE_THEN_RETURN_STR(OMX_CommandMarkBuffer);
            CASE_THEN_RETURN_STR(OMX_CommandMax);
        }
        return "OMX_Command-Unknown";
    }

    static char const* state_str(int x) {
        switch (x) {
            CASE_THEN_RETURN_STR(OMX_StateInvalid);
            CASE_THEN_RETURN_STR(OMX_StateLoaded);
            CASE_THEN_RETURN_STR(OMX_StateIdle);
            CASE_THEN_RETURN_STR(OMX_StateExecuting);
            CASE_THEN_RETURN_STR(OMX_StatePause);
            CASE_THEN_RETURN_STR(OMX_StateWaitForResources);
        }
        return "OMX_State-Unknown";
    }
    static char const* event_str(int x) {
        switch (x) {
            CASE_THEN_RETURN_STR(OMX_EventCmdComplete);
            CASE_THEN_RETURN_STR(OMX_EventError);
            CASE_THEN_RETURN_STR(OMX_EventMark);
            CASE_THEN_RETURN_STR(OMX_EventPortSettingsChanged);
            CASE_THEN_RETURN_STR(OMX_EventBufferFlag);
            CASE_THEN_RETURN_STR(OMX_EventResourcesAcquired);
            CASE_THEN_RETURN_STR(OMX_EventComponentResumed);
            CASE_THEN_RETURN_STR(OMX_EventDynamicResourcesAvailable);
            CASE_THEN_RETURN_STR(OMX_EventPortFormatDetected);
        }
        return "OMX_Event-Unknown";
    }
    static char const* error_str(/*OMX_ERRORTYPE*/int x) {
        switch (x) {
            CASE_THEN_RETURN_STR(OMX_ErrorNone);
            CASE_THEN_RETURN_STR(OMX_ErrorInsufficientResources);
            CASE_THEN_RETURN_STR(OMX_ErrorUndefined);
            CASE_THEN_RETURN_STR(OMX_ErrorInvalidComponentName);
            CASE_THEN_RETURN_STR(OMX_ErrorComponentNotFound);
            CASE_THEN_RETURN_STR(OMX_ErrorInvalidComponent);
            CASE_THEN_RETURN_STR(OMX_ErrorBadParameter);
            CASE_THEN_RETURN_STR(OMX_ErrorNotImplemented);
            CASE_THEN_RETURN_STR(OMX_ErrorUnderflow);
            CASE_THEN_RETURN_STR(OMX_ErrorOverflow);
            CASE_THEN_RETURN_STR(OMX_ErrorHardware);
            CASE_THEN_RETURN_STR(OMX_ErrorInvalidState);
            CASE_THEN_RETURN_STR(OMX_ErrorStreamCorrupt);
            CASE_THEN_RETURN_STR(OMX_ErrorPortsNotCompatible);
            CASE_THEN_RETURN_STR(OMX_ErrorResourcesLost);
            CASE_THEN_RETURN_STR(OMX_ErrorNoMore);
            CASE_THEN_RETURN_STR(OMX_ErrorVersionMismatch);
            CASE_THEN_RETURN_STR(OMX_ErrorNotReady);
            CASE_THEN_RETURN_STR(OMX_ErrorTimeout);
            CASE_THEN_RETURN_STR(OMX_ErrorSameState);
            CASE_THEN_RETURN_STR(OMX_ErrorResourcesPreempted);
            CASE_THEN_RETURN_STR(OMX_ErrorPortUnresponsiveDuringAllocation);
            CASE_THEN_RETURN_STR(OMX_ErrorPortUnresponsiveDuringDeallocation);
            CASE_THEN_RETURN_STR(OMX_ErrorPortUnresponsiveDuringStop);
            CASE_THEN_RETURN_STR(OMX_ErrorIncorrectStateTransition);
            CASE_THEN_RETURN_STR(OMX_ErrorIncorrectStateOperation);
            CASE_THEN_RETURN_STR(OMX_ErrorUnsupportedSetting);
            CASE_THEN_RETURN_STR(OMX_ErrorUnsupportedIndex);
            CASE_THEN_RETURN_STR(OMX_ErrorBadPortIndex);
            CASE_THEN_RETURN_STR(OMX_ErrorPortUnpopulated);
            CASE_THEN_RETURN_STR(OMX_ErrorComponentSuspended);
            CASE_THEN_RETURN_STR(OMX_ErrorDynamicResourcesUnavailable);
            CASE_THEN_RETURN_STR(OMX_ErrorMbErrorsInFrame);
            CASE_THEN_RETURN_STR(OMX_ErrorFormatNotDetected);
            CASE_THEN_RETURN_STR(OMX_ErrorContentPipeOpenFailed);
            CASE_THEN_RETURN_STR(OMX_ErrorContentPipeCreationFailed);
            CASE_THEN_RETURN_STR(OMX_ErrorSeperateTablesUsed);
            CASE_THEN_RETURN_STR(OMX_ErrorTunnelingUnsupported);
        }
        return "OMX_Error-Unknown";
    }

};

char const* index_str(int ix)
{
switch (ix) {
    CASE_THEN_RETURN_STR(OMX_IndexComponentStartUnused);
    CASE_THEN_RETURN_STR(OMX_IndexParamPriorityMgmt);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioInit);
    CASE_THEN_RETURN_STR(OMX_IndexParamImageInit);
    CASE_THEN_RETURN_STR(OMX_IndexParamVideoInit);
    CASE_THEN_RETURN_STR(OMX_IndexParamOtherInit);
    CASE_THEN_RETURN_STR(OMX_IndexParamNumAvailableStreams);
    CASE_THEN_RETURN_STR(OMX_IndexParamActiveStream);
    CASE_THEN_RETURN_STR(OMX_IndexParamSuspensionPolicy);
    CASE_THEN_RETURN_STR(OMX_IndexParamComponentSuspended);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCapturing);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCaptureMode);
    CASE_THEN_RETURN_STR(OMX_IndexAutoPauseAfterCapture);
    CASE_THEN_RETURN_STR(OMX_IndexParamContentURI);
    CASE_THEN_RETURN_STR(OMX_IndexParamCustomContentPipe);
    CASE_THEN_RETURN_STR(OMX_IndexParamDisableResourceConcealment);
    CASE_THEN_RETURN_STR(OMX_IndexConfigMetadataItemCount);
    CASE_THEN_RETURN_STR(OMX_IndexConfigContainerNodeCount);
    CASE_THEN_RETURN_STR(OMX_IndexConfigMetadataItem);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCounterNodeID);
    CASE_THEN_RETURN_STR(OMX_IndexParamMetadataFilterType);
    CASE_THEN_RETURN_STR(OMX_IndexParamMetadataKeyFilter);
    CASE_THEN_RETURN_STR(OMX_IndexConfigPriorityMgmt);
    CASE_THEN_RETURN_STR(OMX_IndexParamStandardComponentRole);

    CASE_THEN_RETURN_STR(OMX_IndexPortStartUnused);
    CASE_THEN_RETURN_STR(OMX_IndexParamPortDefinition);
    CASE_THEN_RETURN_STR(OMX_IndexParamCompBufferSupplier);
    CASE_THEN_RETURN_STR(OMX_IndexReservedStartUnused);

    CASE_THEN_RETURN_STR(OMX_IndexAudioStartUnused);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioPortFormat);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioPcm);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioAac);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioRa);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioMp3);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioAdpcm);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioG723);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioG729);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioAmr);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioWma);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioSbc);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioMidi);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioGsm_FR);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioMidiLoadUserSound);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioG726);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioGsm_EFR);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioGsm_HR);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioPdc_FR);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioPdc_EFR);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioPdc_HR);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioTdma_FR);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioTdma_EFR);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioQcelp8);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioQcelp13);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioEvrc);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioSmv);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioVorbis);
    CASE_THEN_RETURN_STR(OMX_IndexParamAudioFlac);

    CASE_THEN_RETURN_STR(OMX_IndexConfigAudioMidiImmediateEvent);
    CASE_THEN_RETURN_STR(OMX_IndexConfigAudioMidiControl);
    CASE_THEN_RETURN_STR(OMX_IndexConfigAudioMidiSoundBankProgram);
    CASE_THEN_RETURN_STR(OMX_IndexConfigAudioMidiStatus);
    CASE_THEN_RETURN_STR(OMX_IndexConfigAudioMidiMetaEvent);
    CASE_THEN_RETURN_STR(OMX_IndexConfigAudioMidiMetaEventData);
    CASE_THEN_RETURN_STR(OMX_IndexConfigAudioVolume);
    CASE_THEN_RETURN_STR(OMX_IndexConfigAudioBalance);
    CASE_THEN_RETURN_STR(OMX_IndexConfigAudioChannelMute);
    CASE_THEN_RETURN_STR(OMX_IndexConfigAudioMute);
    CASE_THEN_RETURN_STR(OMX_IndexConfigAudioLoudness);
    CASE_THEN_RETURN_STR(OMX_IndexConfigAudioEchoCancelation);
    CASE_THEN_RETURN_STR(OMX_IndexConfigAudioNoiseReduction);
    CASE_THEN_RETURN_STR(OMX_IndexConfigAudioBass);
    CASE_THEN_RETURN_STR(OMX_IndexConfigAudioTreble);
    CASE_THEN_RETURN_STR(OMX_IndexConfigAudioStereoWidening);
    CASE_THEN_RETURN_STR(OMX_IndexConfigAudioChorus);
    CASE_THEN_RETURN_STR(OMX_IndexConfigAudioEqualizer);
    CASE_THEN_RETURN_STR(OMX_IndexConfigAudioReverberation);
    CASE_THEN_RETURN_STR(OMX_IndexConfigAudioChannelVolume);

    CASE_THEN_RETURN_STR(OMX_IndexImageStartUnused);
    CASE_THEN_RETURN_STR(OMX_IndexParamImagePortFormat);
    CASE_THEN_RETURN_STR(OMX_IndexParamFlashControl);
    CASE_THEN_RETURN_STR(OMX_IndexConfigFocusControl);
    CASE_THEN_RETURN_STR(OMX_IndexParamQFactor);
    CASE_THEN_RETURN_STR(OMX_IndexParamQuantizationTable);
    CASE_THEN_RETURN_STR(OMX_IndexParamHuffmanTable);
    CASE_THEN_RETURN_STR(OMX_IndexConfigFlashControl);

    CASE_THEN_RETURN_STR(OMX_IndexVideoStartUnused);
    CASE_THEN_RETURN_STR(OMX_IndexParamVideoPortFormat);
    CASE_THEN_RETURN_STR(OMX_IndexParamVideoQuantization);
    CASE_THEN_RETURN_STR(OMX_IndexParamVideoFastUpdate);
    CASE_THEN_RETURN_STR(OMX_IndexParamVideoBitrate);
    CASE_THEN_RETURN_STR(OMX_IndexParamVideoMotionVector);
    CASE_THEN_RETURN_STR(OMX_IndexParamVideoIntraRefresh);
    CASE_THEN_RETURN_STR(OMX_IndexParamVideoErrorCorrection);
    CASE_THEN_RETURN_STR(OMX_IndexParamVideoVBSMC);
    CASE_THEN_RETURN_STR(OMX_IndexParamVideoMpeg2);
    CASE_THEN_RETURN_STR(OMX_IndexParamVideoMpeg4);
    CASE_THEN_RETURN_STR(OMX_IndexParamVideoWmv);
    CASE_THEN_RETURN_STR(OMX_IndexParamVideoRv);
    CASE_THEN_RETURN_STR(OMX_IndexParamVideoAvc);
    CASE_THEN_RETURN_STR(OMX_IndexParamVideoH263);
    CASE_THEN_RETURN_STR(OMX_IndexParamVideoProfileLevelQuerySupported);
    CASE_THEN_RETURN_STR(OMX_IndexParamVideoProfileLevelCurrent);
    CASE_THEN_RETURN_STR(OMX_IndexConfigVideoBitrate);
    CASE_THEN_RETURN_STR(OMX_IndexConfigVideoFramerate);
    CASE_THEN_RETURN_STR(OMX_IndexConfigVideoIntraVOPRefresh);
    CASE_THEN_RETURN_STR(OMX_IndexConfigVideoIntraMBRefresh);
    CASE_THEN_RETURN_STR(OMX_IndexConfigVideoMBErrorReporting);
    CASE_THEN_RETURN_STR(OMX_IndexParamVideoMacroblocksPerFrame);
    CASE_THEN_RETURN_STR(OMX_IndexConfigVideoMacroBlockErrorMap);
    CASE_THEN_RETURN_STR(OMX_IndexParamVideoSliceFMO);
    CASE_THEN_RETURN_STR(OMX_IndexConfigVideoAVCIntraPeriod);
    CASE_THEN_RETURN_STR(OMX_IndexConfigVideoNalSize);

    CASE_THEN_RETURN_STR(OMX_IndexCommonStartUnused);
    CASE_THEN_RETURN_STR(OMX_IndexParamCommonDeblocking);
    CASE_THEN_RETURN_STR(OMX_IndexParamCommonSensorMode);
    CASE_THEN_RETURN_STR(OMX_IndexParamCommonInterleave);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonColorFormatConversion);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonScale);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonImageFilter);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonColorEnhancement);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonColorKey);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonColorBlend);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonFrameStabilisation);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonRotate);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonMirror);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonOutputPosition);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonInputCrop);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonOutputCrop);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonDigitalZoom);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonOpticalZoom);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonWhiteBalance);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonExposure);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonContrast);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonBrightness);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonBacklight);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonGamma);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonSaturation);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonLightness);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonExclusionRect);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonDithering);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonPlaneBlend);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonExposureValue);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonOutputSize);
    CASE_THEN_RETURN_STR(OMX_IndexParamCommonExtraQuantData);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonFocusRegion);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonFocusStatus);
    CASE_THEN_RETURN_STR(OMX_IndexConfigCommonTransitionEffect);

    CASE_THEN_RETURN_STR(OMX_IndexOtherStartUnused);
    CASE_THEN_RETURN_STR(OMX_IndexParamOtherPortFormat);
    CASE_THEN_RETURN_STR(OMX_IndexConfigOtherPower);
    CASE_THEN_RETURN_STR(OMX_IndexConfigOtherStats);


    CASE_THEN_RETURN_STR(OMX_IndexTimeStartUnused);
    CASE_THEN_RETURN_STR(OMX_IndexConfigTimeScale);
    CASE_THEN_RETURN_STR(OMX_IndexConfigTimeClockState);
    CASE_THEN_RETURN_STR(OMX_IndexConfigTimeActiveRefClock);
    CASE_THEN_RETURN_STR(OMX_IndexConfigTimeCurrentMediaTime);
    CASE_THEN_RETURN_STR(OMX_IndexConfigTimeCurrentWallTime);
    CASE_THEN_RETURN_STR(OMX_IndexConfigTimeCurrentAudioReference);
    CASE_THEN_RETURN_STR(OMX_IndexConfigTimeCurrentVideoReference);
    CASE_THEN_RETURN_STR(OMX_IndexConfigTimeMediaTimeRequest);
    CASE_THEN_RETURN_STR(OMX_IndexConfigTimeClientStartTime);
    CASE_THEN_RETURN_STR(OMX_IndexConfigTimePosition);
    CASE_THEN_RETURN_STR(OMX_IndexConfigTimeSeekMode);


    CASE_THEN_RETURN_STR(OMX_IndexKhronosExtensions);
    CASE_THEN_RETURN_STR(OMX_IndexVendorStartUnused);
    CASE_THEN_RETURN_STR(OMX_IndexVendMtkOmxUpdateColorFormat);
    CASE_THEN_RETURN_STR(OMX_IndexVendorMtkOmxVdecGetColorFormat);
}
return "OMX_Index-Unknown";
}

Image* AvcDecoder::outpoll(Image& img, bool* interrupt)//(uint8_t* out[2]/*, OMX_BUFFERHEADERTYPE* bh*/)
{
    BOOST_ASSERT(impl);
    return impl->outpoll(img, interrupt);
}

void AvcDecoder::input(uint8_t* data, uint8_t* end, int flags) {
    BOOST_ASSERT(impl);
    return impl->input(data,end, flags);
}

bool AvcDecoder::setup(unsigned width, unsigned height, uint8_t* sps[2], uint8_t* pps[2])
{
    if (impl) {
        impl->teardown();
        delete impl;
    }
    impl = new ILAvcDec(COMPONENT_NAME);
    return impl->setup(width,height,sps,pps);
}

AvcDecoder::~AvcDecoder()
{
    if (impl) {
        impl->teardown();
        delete impl;
    }
}

#if 0
int main()
{
    AvcDecoder vdec; //(COMPONENT_NAME); // vdec.test_print_port_params();
    h264nalu_reader freadr; //(fd_ = open("/sdcard/a.h264", O_RDONLY));

    //enum { vWidth = 1280, vHeight = 720 };
    //enum { vWidth = 480, vHeight = 272 };
    enum { vWidth = 1920, vHeight = 1080 };

    if ( freadr.open("/sdcard/a.h264") ) {
        auto sps = freadr.begin(7);
        auto pps = freadr.next(sps, 8);
        if (vdec.setup(vWidth,vHeight, &sps[0], &pps[0])) {
            int nF=30;
            for (auto cur = freadr.begin(5); cur[0] != cur[1]; cur = freadr.next(cur, 5)) {
                vdec.input(cur[0],cur[1]);

                uint8_t* out[2]; //OMX_BUFFERHEADERTYPE bh;
                if (vdec.outpoll(out)) {
                    out;
                }
                if (--nF==0) break;
            }
            // vdec.teardown();
        }
    }

    return 0;
}
#endif

