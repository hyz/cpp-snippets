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
//#include <set>
//#include <memory>
//#include <type_traits>
//#include <list>
#include <vector>
#include <algorithm>
// #include <initializer_list>

#include <OMX_Core.h>
#include <OMX_Component.h>
#include <OMX_Types.h>
//#include "frameworks/av/media/libstagefright/omx/OMXMaster.h"
#include "OMXMaster.hpp"
//#include <utils/Debug.h>
//#include <utils/Mutex.h>

//#define COMPONENT_NAME "OMX.Nvidia.h264.decode"
//#define COMPONENT_NAME "OMX.google.h264.decoder"
#define COMPONENT_NAME "OMX.MTK.VIDEO.DECODER.AVC"

template <typename... As> void err_exit_(int ln, char const* e, char const* fmt, As... a) {
    fprintf(stderr, fmt, ln, e, a...);
    exit(127);
}
template <typename... As> void err_msg_(int ln, char const* e, char const* fmt, As... a) {
    fprintf(stderr, fmt, ln, e, a...);
    fprintf(stderr, "\n");
}
#define ERR_EXIT(...) err_exit_(__LINE__, "", "E%d: " __VA_ARGS__)
#define ERR_MSG(...) err_msg_(__LINE__, "", "E%d:%s: " __VA_ARGS__)
#define ERR_MSG_IF(e, ...) if(e)err_msg_(__LINE__, #e, "E%d:%s: " __VA_ARGS__)
#define ERR_EXIT_IF(e, ...) if(e)err_exit_(__LINE__, #e, "E%d:%s: " __VA_ARGS__)

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

struct h264nalu_reader
{
    typedef std::pair<uint8_t*,uint8_t*> range;
    uint8_t *begin_ = 0, *end_;

    bool open(char const* h264_filename)
    {
        int fd = ::open(h264_filename, O_RDONLY);
        if (fd >= 0) {
            struct stat st; // fd = open(fn, O_RDONLY);
            fstat(fd, &st); // printf("Size: %d\n", (int)st.st_size);

            void* p = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
                void* m = malloc(st.st_size);
                memcpy(m, p, st.st_size);
                begin_ = (uint8_t*)m;
                end_ = begin_ + st.st_size;
            munmap(p, st.st_size);
            assert(begin_[0]=='\0'&& begin_[1]=='\0'&& begin_[2]=='\0'&& begin_[3]==1);

            close(fd);
        }
        ERR_EXIT_IF(begin_==0, "%s: %s", __FUNCTION__, h264_filename);
        return begin_!=0; //std::make_pair(begin_,begin_);
    }

    range next(range r, int type=0) const {
        for (r = find_(r.second); r.first != r.second; r = find_(r.second)) {
            nal_unit_header* h = reinterpret_cast<nal_unit_header*>(r.first+4);
            if (h->nri < 3) {
                continue;
            }
            if (type && h->type != type) {
                printf("nal nri %d type %d\n", int(h->nri), int(h->type));
                continue;
            }
            printf("nal nri %d type %d, len %u\n", int(h->nri), int(h->type), int(r.second-r.first));
            break;
        }
        return r; //find_(begin_);
    }
    range begin(int naltype=0) const {
        return next(std::make_pair(begin_,begin_), naltype);
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
        return std::make_pair(b,e);
    }
};

struct AVCDecoder
{
    typedef AVCDecoder This;

    enum { MSK_USED = 0x10 };
    struct BufferEx {
        OMX_BUFFERHEADERTYPE* buf = 0;
        unsigned char indexTestOnly;
        unsigned char flags = 0;
        BufferEx(OMX_BUFFERHEADERTYPE* b=0) { indexTestOnly=flags=0; buf=b; }
    };

    struct IOPort : OMX_PARAM_PORTDEFINITIONTYPE , std::vector<BufferEx> {
        IOPort(unsigned char portIdx=0) /*: std::vector<BufferWrap>(24)*/ {
            nPortIndex = portIdx;
        }
        //~IOPort() { omxbufs_deinit(component); }
        //OMX_BUFFERHEADERTYPE* buf(unsigned x) const { return (*this)[x].buf; }

        int index(iterator it) const { return int(it - begin()); }
        iterator iterator_to(BufferEx& x) { return begin() + (&x - &front()); }

        int countf(unsigned msk) const {
            int n = 0;
            for (auto& b : *this) {
                if (b.flags & msk)
                    ++n;
            }
            return n;
        }

        void print() const {
            OMX_PARAM_PORTDEFINITIONTYPE const& def = *this;
            OMX_VIDEO_PORTDEFINITIONTYPE const& vdef = def.format.video;
            printf("Port %d nBufferCount(Actual/Min) %d %d, %.1fK, Enabled %d, Populated %d"
                        "\n\t%s, %ux%u %u %u, xFrate %u, nBrate %u, color %u, compress %u\n"
                    , def.nPortIndex, def.nBufferCountActual,def.nBufferCountMin, def.nBufferSize/1024.0, def.bEnabled, def.bPopulated
                    , vdef.cMIMEType
                    , vdef.nFrameWidth, vdef.nFrameHeight, vdef.nStride, vdef.nSliceHeight
                    , vdef.xFramerate, vdef.nBitrate
                    , vdef.eColorFormat, vdef.eCompressionFormat //OMX_COLOR_FormatYUV420Planar=19,OMX_VIDEO_CodingAVC=7
                    );
        }

    }; // ioports[2] = {0,1};
    typedef IOPort::iterator iterator;
    typedef std::pair<uint8_t*,uint8_t*> range;

    android::OMXMaster omxMaster; // err = OMX_Init();
    IOPort input_port{0};
    IOPort output_port{1};

    OMX_HANDLETYPE component; //OMX_COMPONENTTYPE *component;
    OMX_STATETYPE state;
    unsigned char stage_ = 0;

    sem_t sem_command_;
    sem_t sem_bufempty_;
    //sem_t sem_buffilled_;
    //std::list<iterator> oseq_;
    //android::Mutex mMutex;
    iterator icur_; // = output_port.begin();

    AVCDecoder(char const* component_name)
    {
        //static_assert(std::is_same<OMX_HANDLETYPE,OMX_COMPONENTTYPE*>::value);
        static OMX_CALLBACKTYPE callbacks = {
            .EventHandler = &cbEventHandler0,
            .EmptyBufferDone = &cbEmptyThisBuffer0,
            .FillBufferDone = &cbFillThisBuffer0
        };
        sem_init(&sem_command_, 0, 0);
        sem_init(&sem_bufempty_, 0, 0);
        //sem_init(&sem_buffilled_, 0, 0);

        OMX_COMPONENTTYPE *handle;
        OMX_ERRORTYPE err = omxMaster.makeComponentInstance(component_name, &callbacks, this, &handle);
        ERR_MSG_IF(err!=OMX_ErrorNone, "makeComponentInstance");
        component = /*(OMX_HANDLETYPE)*/handle; // OMX_GetHandle
    }
    ~AVCDecoder() {
        if (component)
            omxMaster.destroyComponentInstance((OMX_COMPONENTTYPE*)component);
        sem_destroy(&sem_command_);
        sem_destroy(&sem_bufempty_);
        //sem_destroy(&sem_buffilled_);
        printf("~\n");
    }

    void teardown() {
        stage_ = 3;
        while (sem_trywait(&sem_bufempty_) == 0)
            ;
        assert(errno == EAGAIN);
        while (sem_trywait(&sem_command_) == 0)
            ;
        assert(errno == EAGAIN);

        printf("===%s\n",__FUNCTION__);
        command(OMX_CommandFlush, output_port.nPortIndex, NULL).wait();
        command(OMX_CommandStateSet, OMX_StateIdle, NULL).wait();
        command(OMX_CommandStateSet, OMX_StateLoaded, NULL)([this](){
                omxbufs_deinit(input_port);
                omxbufs_deinit(output_port);
            }).wait();
    }

    bool setup(unsigned vWidth, unsigned vHeight, range sps, range pps)
    {
        //// OMX_IndexParamVideoInit, OMX_PORT_PARAM_TYPE
        //enum { vWidth = 1280, vHeight = 720 };
        //enum { vWidth = 480, vHeight = 272 };
        // OMX_VIDEO_PARAM_PROFILELEVELTYPE

        sync_state();

        {
            OMX_PARAM_COMPONENTROLETYPE roleParams;
            android::InitOMXParams(&roleParams);
            strncpy((char *)roleParams.cRole, "video_decoder.avc", OMX_MAX_STRINGNAME_SIZE - 1);
            roleParams.cRole[OMX_MAX_STRINGNAME_SIZE - 1] = '\0';
            OMX_ERRORTYPE err = OMX_SetParameter(component, OMX_IndexParamStandardComponentRole, &roleParams);
            ERR_EXIT_IF(err!=OMX_ErrorNone, "OMX_SetParameter OMX_IndexParamStandardComponentRole");
        } {
            OMX_VIDEO_PARAM_PORTFORMATTYPE format;
            android::InitOMXParams(&format)->nPortIndex = 0; //kPortIndexInput;
            format.nIndex = 0;
            for ( ;; ) {
                OMX_ERRORTYPE err = OMX_GetParameter( component, OMX_IndexParamVideoPortFormat, &format);
                ERR_EXIT_IF(err!=OMX_ErrorNone, "OMX_GetParameter OMX_IndexParamVideoPortFormat");
                if (format.eCompressionFormat == OMX_VIDEO_CodingAVC && format.eColorFormat == OMX_COLOR_FormatUnused) {
                    OMX_SetParameter(component, OMX_IndexParamVideoPortFormat, &format);
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
            FillOneBuffer();

pos_test_again_:
        /*(sps.first != sps.second)*/ {
#if 0
            static unsigned char sps_[] = { 0,0,0,1,
                0x67, 0x64, 0x00, 0x28, 0xac, 0xf0, 0x1e, 0x00, 0x89, 0xf9, 0x50
              //0x67, 0x64, 0x00, 0x28, 0xac, 0xe8, 0x07, 0x80, 0x22, 0x7e, 0x58, 0x02
            };
            sps.first = sps_;
            sps.second = &sps_[sizeof(sps_)];
#endif
            EmptyThis(sps.first, sps.second, OMX_BUFFERFLAG_CODECCONFIG|OMX_BUFFERFLAG_ENDOFFRAME);
        } {
            EmptyThis(pps.first, pps.second, OMX_BUFFERFLAG_CODECCONFIG|OMX_BUFFERFLAG_ENDOFFRAME);
        }

        CommandHelper{this,OMX_CommandMax,OMX_EventPortSettingsChanged,0}.wait();
        printf("===%d OMX_EventPortSettingsChanged ...\n",__LINE__);

        sync_port_definition(output_port);
        command(OMX_CommandPortDisable, output_port.nPortIndex, NULL)([this](){
                    if (output_port.countf(MSK_USED)>0)
                        CommandHelper{this,OMX_CommandMax,OMX_EventBufferFlag,0}.wait(); //XXX
    printf("===%d OMX_EventBufferFlag %x ...\n",__LINE__, output_port.countf(MSK_USED));
                    omxbufs_deinit(output_port);
                }).wait();
    printf("===%d OMX_CommandPortDisable OK\n",__LINE__);

        sync_port_definition(output_port);
        command(OMX_CommandPortEnable, output_port.nPortIndex, NULL)([this](){
                    omxbufs_reinit(output_port);
                }).wait();
        printf("===%d OMX_CommandPortEnable OK\n",__LINE__);//sleep(1);

        sync_port_definition(input_port);
        sync_port_definition(output_port); //sync_state();

        //FillOneBuffer();
        //for (int i=0; ; ++i)
        //    if (!FillOneBuffer()) {
        //        printf("FillOneBuffer : %d\n", i);
        //        break;
        //    }
//goto pos_test_again_;

        while (sem_trywait(&sem_bufempty_) == 0)
            printf("sem_trywait(&sem_bufempty_) == 0\n");
        assert(errno == EAGAIN);
        while (sem_trywait(&sem_command_) == 0)
            printf("sem_trywait(&sem_command_) == 0\n");
        assert(errno == EAGAIN);

        stage_ = 1;
        printf("===%d:%s pre-dec-loop\n",__LINE__,__FUNCTION__); //sleep(1);
        for (int x=2; x>0; --x) {
            FillOneBuffer();
            sem_wait(&sem_command_);
        }
        sem_wait(&sem_bufempty_);
        sem_wait(&sem_bufempty_);
        while (sem_trywait(&sem_command_) == 0)
            ;
        while (sem_trywait(&sem_bufempty_) == 0)
            ;

        printf("===%d:%s dec-loop\n",__LINE__,__FUNCTION__); //sleep(1);
        stage_ = 2;
        for (auto beg = output_port.begin(); beg != output_port.end(); ++beg) {
            FillThisBuffer(beg);
            //oseq_.push_back(beg);
        }
        icur_ = output_port.begin();

        return true;
    }

    OMX_BUFFERHEADERTYPE* decode(range rng, OMX_BUFFERHEADERTYPE* buf)
    {
        EmptyThis(rng.first,rng.second, OMX_BUFFERFLAG_ENDOFFRAME|OMX_BUFFERFLAG_DECODEONLY); //OMX_BUFFERFLAG_DECODEONLY//OMX_BUFFERFLAG_ENDOFFRAME
        sem_wait(&sem_bufempty_);

        iterator it = icur_++;
        if (icur_ == output_port.end()) {
            icur_ = output_port.begin();
        }

        int npTest=0;
        for (; it->buf->nFilledLen <= 0; ++npTest) {
            usleep(1000);
        }
        nTst.nPoll++;
        *buf = *it->buf; //OMX_BUFFERHEADERTYPE bh = *it->buf;

        static int npTest1 = 0;
        if (npTest1 < npTest) {
            npTest1 = npTest;
        }
printf("poll: P1[%d] %d %d-%d-%d, %u\n", INDEX(it), buf->nFilledLen, nTst.cbEmpty,nTst.nPoll,nTst.cbFill, npTest1);

        //for (int npTest=0; ; ++npTest) {
        //    auto iti = std::find_if(oseq_.begin(),oseq_.end(), [](iterator i){return i->buf->nFilledLen>0;});
        //    if (iti != oseq_.end()) {
        //        it = *iti;
        //        android::Mutex::Autolock autoLock(mMutex);
        //        oseq_.erase(iti);
        //        break;
        //    }
        //    usleep(5000);
        //}

        //appendfile("/sdcard/o.yuv", buf->pBuffer, 1920*1080);
        return buf;
    } // decode_start

private:
    void appendfile(char const* fn, void* buf, unsigned len)
    {
        if (FILE* fp = fopen(fn, "a")) {
            fwrite(buf, len, 1, fp);
            fclose(fp);
        }
    }

    OMX_STATETYPE sync_state() {
        OMX_STATETYPE state0 = state;
        OMX_GetState(component, &state);
        printf("state %d:%s => %d:%s\n", state0, state_str(state0), state, state_str(state));
        return state;
    }

    void sync_port_definition(IOPort& iop) {
        int portIndex = iop.nPortIndex;

        OMX_PARAM_PORTDEFINITIONTYPE* def = &iop;
        android::InitOMXParams(def)->nPortIndex = portIndex;
        OMX_ERRORTYPE err=OMX_GetParameter(component, OMX_IndexParamPortDefinition, def);
        ERR_EXIT_IF(err!=OMX_ErrorNone, "OMX_GetParameter");

        iop.reserve(iop.nBufferCountActual);

        iop.print();
    }

    void sync_port_definition(IOPort& iop, OMX_PARAM_PORTDEFINITIONTYPE const& def) {
        OMX_ERRORTYPE err=OMX_SetParameter(component
                , OMX_IndexParamPortDefinition, const_cast<OMX_PARAM_PORTDEFINITIONTYPE*>(&def));
        ERR_EXIT_IF(err!=OMX_ErrorNone, "OMX_SetParameter");
        sync_port_definition(iop);
    }

    void omxbufs_deinit(IOPort& iop) {
        printf("%s: Port %d: Actual %u, %d\n",__FUNCTION__, iop.nPortIndex, iop.nBufferCountActual, (int)iop.size());
        for (auto beg = iop.begin(); beg != iop.end(); ++beg) {
            OMX_FreeBuffer(component, iop.nPortIndex, beg->buf);
        }
        iop.clear();
    }
    void omxbufs_reinit(IOPort& iop)/*(OMX_HANDLETYPE component)*/ {
        if (!iop.empty())
            omxbufs_deinit(iop);
        printf("%s: Port %d: Actual %u\n",__FUNCTION__, iop.nPortIndex, iop.nBufferCountActual);

        iop.resize(iop.nBufferCountActual);
        for (auto beg = iop.begin(); beg != iop.end(); ++beg) {
            *beg = BufferEx();
            beg->indexTestOnly = (unsigned char)(beg - iop.begin());
            beg->buf = omxbuf_alloc(iop, beg);
            ERR_EXIT_IF(!beg->buf, "%s",__FUNCTION__);
        }
    }

    struct CommandHelper
    {
        This* self;
        OMX_COMMANDTYPE Cmd; OMX_U32 nParam1; OMX_PTR pCmdData;

        CommandHelper const& send() const {
            print(__FUNCTION__);
            if (Cmd < OMX_CommandMax) {
                OMX_ERRORTYPE err = OMX_SendCommand(self->component, Cmd, nParam1, pCmdData);
                ERR_EXIT_IF(err!=OMX_ErrorNone, "OMX_SendCommand");
            }
            return *this;
        }
        template <typename F> CommandHelper const& operator()(F&& fn) const {
            fn();
            return *this;
        }
        void wait() const {
            print(__FUNCTION__);
            int ret = sem_wait(&self->sem_command_); // OMX_StateExecuting OMX_StateWaitForResources
            ERR_EXIT_IF(ret<0, "sem_wait");
            if (OMX_CommandStateSet == nParam1)
                self->sync_state();
            print(__FUNCTION__, " [OK]\n");
        }
        void print(char const* pfx, char const* end="\n") const {
            if (Cmd == OMX_CommandStateSet)
                printf("%s:%s: %s%s", pfx, self->cmd_str(Cmd), self->state_str(nParam1), end);
            else if (Cmd == OMX_CommandMax)
                printf("%s:%s: %u%s", pfx, self->event_str(nParam1), nParam1, end);
            else
                printf("%s:%s: %u%s", pfx, self->cmd_str(Cmd), nParam1, end);
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

    iterator FindFreeBuffer(IOPort& iop) {
        printf("%s P%d, %u\n",__FUNCTION__,iop.nPortIndex, iop.size());
        for (auto beg = iop.begin(); beg != iop.end(); ++beg) {
            if (0==(beg->flags & MSK_USED)) {
                return beg;//->buf; //OMX_BUFFERHEADERTYPE* bh = b.buf; //iop.buf(i);
            }
        }
        return iop.end();
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
        it->flags |= MSK_USED;
printf("%s:%s [%d] %d %d %x\n", __FUNCTION__,"OMX_FillThisBuffer", INDEX(it), bh->nFilledLen, bh->nOffset, bh->nFlags);
        OMX_ERRORTYPE err = OMX_FillThisBuffer(component, bh);
        ERR_EXIT_IF(err!=OMX_ErrorNone, "OMX_FillThisBuffer %d", int(err));
    }

    iterator FillOneBuffer() {
        auto it = FindFreeBuffer(output_port);
        if (it != output_port.end()) {
            FillThisBuffer(it);
        }
        return it;
    }

    iterator EmptyThis(uint8_t const* beg, uint8_t const* end, int flags) {
        auto it = FindFreeBuffer(input_port);
        if (it == input_port.end()) {
            ERR_EXIT("%s",__FUNCTION__);
        } else {
            OMX_BUFFERHEADERTYPE* bh = it->buf;
            bh->nFlags = flags;
            bh->nOffset = 0;
            bh->nFilledLen = end - beg;
            memcpy(bh->pBuffer, beg, bh->nFilledLen);
            // OMX_BUFFERFLAG_CODECCONFIG OMX_BUFFERFLAG_ENDOFFRAME OMX_BUFFERFLAG_DECODEONLY
            it->flags |= MSK_USED;
            OMX_ERRORTYPE err = OMX_EmptyThisBuffer(component, bh);
            ERR_EXIT_IF(err!=OMX_ErrorNone, "OMX_EmptyThisBuffer %d", int(err));
            printf("%s: P0[%d] len %u\n",__FUNCTION__, INDEX(it), bh->nFilledLen);
        }
        return it;
    }
    //void EmptyThisBuffer(OMX_BUFFERHEADERTYPE* bh) {
    //    // OMX_BUFFERFLAG_CODECCONFIG OMX_BUFFERFLAG_ENDOFFRAME OMX_BUFFERFLAG_DECODEONLY
    //    |= MASK(bh);
    //    OMX_ERRORTYPE err = OMX_EmptyThisBuffer(component, bh);
    //    ERR_EXIT_IF(err!=OMX_ErrorNone, "OMX_EmptyThisBuffer %d", int(err));
    //    printf("%s: P0[%d] len %u\n",__FUNCTION__, INDEX(bh), bh->nFilledLen);
    //}

private:
    //static inline unsigned MASK(OMX_BUFFERHEADERTYPE* bh) { return (1<<INDEX(bh)); }
    static inline unsigned INDEX(iterator it) {
        return (it->indexTestOnly); //(it->buf->nPortIndex==0) ? input_port.index(it) : output_port.index(it);
        //return (unsigned(bh->pAppPrivate)&0x3f);
    }

    OMX_BUFFERHEADERTYPE* omxbuf_alloc(IOPort& port, iterator it) {
        OMX_BUFFERHEADERTYPE* bh = 0;
        OMX_PARAM_PORTDEFINITIONTYPE* def = &port;
        //if (bidx < def->nBufferCountActual && bidx < port.size()) {
        BufferEx* ptr = &*it; //.operator->();
        OMX_ERRORTYPE err = OMX_AllocateBuffer(component, &bh, def->nPortIndex, (OMX_PTR)ptr, def->nBufferSize); // pAppPrivate
        ERR_EXIT_IF(err!=OMX_ErrorNone, "OMX_AllocateBuffer P%d[%d] %u: %s", def->nPortIndex, INDEX(it), def->nBufferSize, error_str(err));
        printf("%s:OMX_AllocateBuffer P%d[%d] %u\n",__FUNCTION__, def->nPortIndex, INDEX(it), def->nBufferSize);
        //}
        return bh;
    }

private: // callbacks
    struct statisCount {
        int nPoll = 0;
        int cbFill = 0;
        int cbEmpty = 0;
    } nTst;

    OMX_ERRORTYPE cbEmptyThisBuffer(iterator it)
    {
        OMX_BUFFERHEADERTYPE* bh = it->buf;
        if (bh->nFilledLen > 100)
            nTst.cbEmpty++;
        printf("%s: P0[%d] %u %u %u, %d-%d\n",__FUNCTION__, INDEX(it), bh->nFilledLen, bh->nOffset, bh->nAllocLen, nTst.cbEmpty,nTst.cbFill);

        it->flags &= ~MSK_USED;
        sem_post(&sem_bufempty_);

        return OMX_ErrorNone;
    }

    OMX_ERRORTYPE cbFillThisBuffer(iterator it)
    {
        if (stage_ >= 3)
            return OMX_ErrorNone;
        OMX_BUFFERHEADERTYPE* bh = it->buf;

        if (bh->nFilledLen > 100)
            nTst.cbFill++;
        printf("%s: P1[%d] %u %u %u, %d %d-%d\n",__FUNCTION__, INDEX(it), bh->nFilledLen, bh->nOffset, bh->nAllocLen, int(stage_), nTst.cbEmpty,nTst.cbFill);

        if (bh->nFilledLen <= 0) {
            it->flags &= ~MSK_USED;
            if (stage_==1 || (stage_==0 && output_port.countf(MSK_USED) == 0)) {
                printf("%s: P1[%d] stage=%d\n",__FUNCTION__, INDEX(it), int(stage_));
                sem_post(&sem_command_); // OMX_EventBufferFlag
            }
        } else {
            FillThisBuffer(it);
            ///android::Mutex::Autolock autoLock(mMutex);
            ///oseq_.push_back(it);
            ////it->flags &= ~MSK_USED;
        }
        return OMX_ErrorNone;
    }

    OMX_ERRORTYPE cbEventHandler(OMX_OUT OMX_EVENTTYPE eEvent
            , OMX_OUT OMX_U32 Data1, OMX_OUT OMX_U32 Data2, OMX_OUT OMX_PTR pEventData)
    {
        printf("%s: %d:%s %d %d %p\n",__FUNCTION__, int(eEvent),event_str(eEvent), (int)Data1, (int)Data2, pEventData);

        switch (eEvent) {
            case OMX_EventCmdComplete:
                switch ((OMX_COMMANDTYPE)Data1) {
                    case OMX_CommandStateSet:
                        printf("%s: OMX_EventCmdComplete:OMX_CommandStateSet %d:%s\n",__FUNCTION__, (int)Data2,state_str(Data2));
                        sem_post(&sem_command_);
                        break;
                    case OMX_CommandPortDisable:
                        printf("%s: OMX_EventCmdComplete:OMX_CommandPortDisable %d %d\n",__FUNCTION__, (int)Data1, (int)Data2);
                        sem_post(&sem_command_);
                        break;
                    case OMX_CommandPortEnable:
                        printf("%s: OMX_EventCmdComplete:OMX_CommandPortEnable %d %d\n",__FUNCTION__, (int)Data1, (int)Data2);
                        sem_post(&sem_command_);
                        break;
                    case OMX_CommandFlush:
                        printf("%s: OMX_EventCmdComplete:OMX_CommandFlush %d %d\n",__FUNCTION__, (int)Data1, (int)Data2);
                        sem_post(&sem_command_);
                        break;
                }
                break;
            case OMX_EventPortSettingsChanged: // TODO
                printf("%s: OMX_EventPortSettingsChanged %d %d\n",__FUNCTION__, (int)Data1, (int)Data2);
                sem_post(&sem_command_);
                break;
            case OMX_EventBufferFlag: ///**< component has detected an EOS */
                ERR_EXIT("%s: OMX_EventBufferFlag",__FUNCTION__);
            case OMX_EventError: ///**< component has detected an error condition */
                ERR_EXIT("%s: OMX_EventError",__FUNCTION__);
        }
        return OMX_ErrorNone;
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

private:
    static OMX_ERRORTYPE cbEventHandler0(OMX_OUT OMX_HANDLETYPE hComponent, OMX_OUT OMX_PTR pAppData
            , OMX_OUT OMX_EVENTTYPE eEvent, OMX_OUT OMX_U32 Data1, OMX_OUT OMX_U32 Data2, OMX_OUT OMX_PTR pEventData)
    {
        ERR_MSG_IF(hComponent!=static_cast<This*>(pAppData)->component, "%p %p", hComponent,pAppData);
        return static_cast<This*>(pAppData)->cbEventHandler(eEvent, Data1, Data2, pEventData);
    }
    static OMX_ERRORTYPE cbEmptyThisBuffer0(OMX_OUT OMX_HANDLETYPE hComponent, OMX_OUT OMX_PTR pAppData
            , OMX_OUT OMX_BUFFERHEADERTYPE* pBuffer)
    {
        ERR_MSG_IF(hComponent!=static_cast<This*>(pAppData)->component, "%p %p", hComponent,pAppData);
        assert(pBuffer);
        This* thiz = static_cast<This*>(pAppData);
        BufferEx* bx = static_cast<BufferEx*>(pBuffer->pAppPrivate);
        return thiz->cbEmptyThisBuffer( thiz->input_port.iterator_to(*bx) );
    }
    static OMX_ERRORTYPE cbFillThisBuffer0(OMX_OUT OMX_HANDLETYPE hComponent, OMX_OUT OMX_PTR pAppData
            , OMX_OUT OMX_BUFFERHEADERTYPE* pBuffer)
    {
        ERR_MSG_IF(hComponent!=static_cast<This*>(pAppData)->component, "%p %p", hComponent,pAppData);
        assert(pBuffer);
        //return static_cast<This*>(pAppData)->cbFillThisBuffer(*static_cast<BufferEx*>(pBuffer->pAppPrivate));//((unsigned)pBuffer->pAppPrivate);
        This* thiz = static_cast<This*>(pAppData);
        BufferEx* bx = static_cast<BufferEx*>(pBuffer->pAppPrivate);
        return thiz->cbFillThisBuffer( thiz->output_port.iterator_to(*bx) );
    }
};

int main()
{
    AVCDecoder dec(COMPONENT_NAME); // dec.test_print_port_params();
    h264nalu_reader filer; //(fd_ = open("/sdcard/a.h264", O_RDONLY));

    if ( filer.open("/sdcard/a.h264") ) {
        auto sps = filer.begin(7);
        auto pps = filer.next(sps, 8);
        if (dec.setup(1920,1080, sps, pps)) {
            int nF=30;
            for (auto cur = filer.begin(5); cur.first != cur.second; cur = filer.next(cur, 5)) {
                OMX_BUFFERHEADERTYPE out;
                if (dec.decode(cur, &out)) {
                    out;
                }
                if (--nF==0) break;
            }
            dec.teardown();
        }
    }

    return 0;
}

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

