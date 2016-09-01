#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <pthread.h> // #include <mutex>
#include <memory>
#include <array>
#include <vector>
#include <deque>
#include <algorithm>
#include <boost/assert.hpp>
#include <boost/noncopyable.hpp>
#include <jni.h>
#include <android/native_window_jni.h>

#include "VideoRender.h"
#include "ildec.h"
#include "clock.hpp"
#include "thread.hpp"
#include "hgs.hpp"

#ifdef TEST_WITH_VIDEO_FILE
#  define ENABLE_TEST_JNICALL
#  define ENABLE_TEST_MAIN
#endif
#undef   LOG_TAG
#define  LOG_TAG "HGSJNI"
#include "alog.hpp"

typedef clock_realtime_type Clock;

#if 0 //defined(__ANDROID__)
#   define _TRACE_SIZE_PRINT() ((void)0)
#   define _TRACE_NET_INCOMING(type, size) ((void)0)
//#   define _TRACE_DROP0(n) ((void)0)
//#   define _TRACE_DROP1(n) ((void)0)
#   define _TRACE_DROP_non_IDR(nri, type, n_fwd) ((void)0)
#   define _TRACE_DEC_INC(nri,type,siz) ((void)0)
#   define _TRACE_OUT_INC(siz) ((void)0)
#   define _TRACE_RESET() ((void)0)
#   define _TRACE_PRINT_RATE() ((void)0)
#else
static struct Trace_Info {
    struct TraceSize {
        unsigned len_max=0;
        unsigned len_total=0;
        unsigned count=0;
    };
    std::array<TraceSize,32> sa = {};
    struct { unsigned nfr, dec, out; } ns = {}; // non_IDR_;
    Clock::time_point tp{};
} tc[2] = {};

static void _TRACE_RESET() { tc[1] = tc[0] = Trace_Info{}; }

static void _TRACE_SIZE_PRINT() {
    for (unsigned i=0; i<tc[0].sa.size(); ++i) {
        auto& a = tc[0].sa[i];
        if (a.count > 0) {
            LOGD("NAL-Unit-Type %d: m/a/n: %u %u %u", i, a.len_max, a.len_total/a.count, a.count);
        }
    }
}
static void _TRACE_NET_INCOMING(int type, unsigned size) {
    auto& a = tc[0].sa[type];
    a.len_max = std::max(size, a.len_max);
    a.len_total += size;
    a.count++;

    if (type==1 || type==5) {
        tc[0].ns.nfr++;
    }
}
//static void _TRACE_DROP0(unsigned n) {}
//static void _TRACE_DROP1(unsigned n) { tc[0].n_drop1_ += n; }
// static void _TRACE_DROP_non_IDR(int nri, int type, unsigned nfwd) { }
static void _TRACE_DEC_INC(int nri, int type,unsigned siz) {
    if (type == 5)
        tc[0].ns.dec++;
    else
        LOGD("FWD %d", type);
}
static void _TRACE_OUT_INC(unsigned siz) {
    tc[0].ns.out++;
}
static void _TRACE_PRINT_RATE() {
    tc[0].tp = Clock::now();
    unsigned ms = milliseconds(tc[0].tp - tc[1].tp);
    if (ms > 3000) {
        LOGD("F-rate: %.2f/%.2f, %u%+d%+d"
                , (tc[0].ns.nfr - tc[1].ns.nfr)*1000.0/ms
                , (tc[0].ns.out - tc[1].ns.out)*1000.0/ms
                , tc[0].ns.nfr, -int(tc[0].ns.nfr-tc[0].ns.dec), -int(tc[0].ns.dec-tc[0].ns.out));
        tc[1] = tc[0];
    }
}
#endif

struct Main : boost::noncopyable
{
    struct Args {
        VideoRender vgl;
        AvcDecoder vdec;
        std::vector<uint8_t> sps, pps;
        unsigned short vWidth, vHeight;
        bool setup_ok_ = 0;
        bool renderer_ok_ = 0;

        bool init(mbuffer&& b) {
            auto* h = b.nal_header();
            if (h->nri < 3) {
                return 0;
            }
            if (h->type == 7) {
                sps = std::vector<uint8_t>(b.begin_startbytes(), b.end());
            } else if (h->type == 8) {
                pps = std::vector<uint8_t>(b.begin_startbytes(), b.end());
            } else {
                return 0;
            }
            // LOGD("sps %u, pps %u", sps.size(), pps.size());
            return (!sps.empty() && !pps.empty());
        }
    };

    Main(std::unique_ptr<Args>&& args)
        : thread(*this, "Thread:Main"), args_(std::move(args))
    {
        ERR_EXIT_IF(!args_, "!args");

        auto& a = *args_;
        uint8_t* sps[2] = { &a.sps.front(), &a.sps.back()+1 };
        uint8_t* pps[2] = { &a.pps.front(), &a.pps.back()+1 };
        if (a.vdec.setup(a.vWidth,a.vHeight, sps, pps)) {
            args_->setup_ok_ = 1;
        } else {
            ERR_MSG("decode:setup");
        }
        LOGD("decode:setup [OK]");
    }
    //~Main() {}

    void input(mbuffer&& b) {
        auto* h = b.nal_header();
        if (h->nri < 3 || h->type != 5) {
            return;
        }
        //iseq_++;
        //tp0 = Clock::now();
        //isize_ = b.end() - b.begin_startbytes();
        args_->vdec.input(b.begin_startbytes(), b.end());
    }

    Thread<Main> thread;

private:
    //unsigned iseq_ = 0, oseq_ = 0, isize_;
    //Clock::time_point tp0;
    std::unique_ptr<Args> args_;

private:
    friend struct Thread<Main>;
    void run()
    {
        if (!args_->setup_ok_) {
            ERR_MSG("Main thread: setup fail");
            return;
        }
        LOGD("Main thread");

        auto& a = *args_;
        unsigned nF = 0;
        while (!thread.stopped) {
            Image img = {}; // OMX_BUFFERHEADERTYPE bh;
            if (!a.vdec.outpoll(img, &thread.stopped)) {
                ERR_MSG("%d !vdec:poll", (int)thread.stopped);
                break;
            }
            ++nF;

            static Image copy = img;
            auto tp = Clock::now();
            if (copy.pdata == img.pdata) {
                copy.porg = copy.pdata = (uint8_t*)malloc(img.size);//(img.pdata,  img.size);
            }
            if (copy.size >= img.size) {
                copy.size = img.size;
                memcpy(copy.pdata, img.pdata, img.size);
            }
            LOGD("copy mills %u", milliseconds(Clock::now()-tp));

            if (a.renderer_ok_) {
                auto tp = Clock::now();
                a.vgl.renderFrame(&img);
                LOGD("renderFrame %u", milliseconds(Clock::now()-tp));
            }
        }

        LOGD("Main End: nF %u, stopped %d", nF, int(thread.stopped));
    }
};
//struct PreMain : Main::Args { };

//enum { BUFFER_FLAG_CODEC_CONFIG =2 }; //c++: BUFFER_FLAG_CODECCONFIG =2 //OMX: OMX_BUFFERFLAG_CODECCONFIG
//
//static JavaVM *  jvm_ = NULL;
//static JNIEnv *  env_= NULL;
//static jobject   oDecoderWrap = NULL;
//static jclass    CLS_DecoderWrap = 0;
//static jmethodID MID_DecoderWrap_close = 0;
//static jmethodID MID_DecoderWrap_ctor = 0;
//static jmethodID MID_IBufferobtain  = 0;
//static jmethodID MID_IBufferinflate = 0;
//static jmethodID MID_IBufferrelease = 0;
//static jmethodID MID_OBufferobtain  = 0;
//static jmethodID MID_OBufferrelease = 0;
//static jfieldID  FID_outputBuffer   = 0;

static signed char stage_ = 0;

struct nalu_data_sink;
//static std::unique_ptr<nalu_data_sink> sink_;
static std::unique_ptr<Main::Args> tmpargs_;
static std::unique_ptr<Main> main_;

static void nwk_dataincoming(mbuffer b)
{
    if (main_) {
        main_->input(std::move(b));
    } else if (tmpargs_) {
        if (tmpargs_->init(std::move(b))) {
            main_.reset( new Main(std::move(tmpargs_)) );
            main_->thread.start();
        }
    } else {
        ERR_EXIT("error");
    }
}

#ifdef TEST_WITH_VIDEO_FILE
struct TestMain
{
    struct h264nalu_reader
    {
        typedef std::array<uint8_t*,2> range;
        uint8_t *begin_ = 0, *end_ = 0;

        bool open(char const* h264_filename)
        {
            int fd = ::open(h264_filename, O_RDONLY);
            if (fd >= 0) {
                struct stat st; // fd = open(fn, O_RDONLY);
                fstat(fd, &st); // LOGD("Size: %d\n", (int)st.st_size);

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

        range next(range const& r, int type=0) const { return next(r[1], type); }
        range next(uint8_t* p, int type=0) const {
            range r = find_(p);
            for (; r[0] != r[1]; r = find_(r[1])) {
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
            return next(begin_, naltype);
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
            return {b,e};
        }
    };

    TestMain() : thread(*this, "Thread:Test") {}

    Thread<TestMain> thread;

    void run() {
        h264nalu_reader freadr; //(fd_ = open("/sdcard/a.h264", O_RDONLY));
        unsigned nF = 0;

        if ( !freadr.open("/sdcard/a.h264") ) {
            ERR_EXIT("fopen");
        } else {
            auto p = freadr.begin(7);
            nwk_dataincoming( make_mbuffer(p[0], p[1]) );
            p = freadr.next(p, 8);
            nwk_dataincoming( make_mbuffer(p[0], p[1]) );

            //sleep(1);
            for (auto cur = freadr.begin(5); cur[0] != cur[1]; cur = freadr.next(cur, 5)) {
                nwk_dataincoming( make_mbuffer(cur[0],cur[1]) );
                ++nF;
                usleep(30000);
            }
        }
        LOGD("TestMain End: nF %u", nF);
    }

    static mbuffer make_mbuffer(uint8_t* data, uint8_t* end) {
        data += 4;
        return mbuffer(rtp_header{}, (nal_unit_header*)data, end);
    }
};
static std::unique_ptr<TestMain> test_;
#endif

inline int javacodec_ibuffer_obtain(int timeout)
{
    //int idx = env_->CallIntMethod(oDecoderWrap, MID_IBufferobtain, timeout);
    //jthrowable ex = env_->ExceptionOccurred();
    //if (ex != NULL) {
    //    env_->ExceptionDescribe();
    //    env_->ExceptionClear();
    //}
    return 0;//idx;
}
inline void javacodec_ibuffer_inflate(int idx, char* p, size_t len)
{
    //jobject byteBuffer = env_->NewDirectByteBuffer(p, len);
    //env_->CallVoidMethod(oDecoderWrap, MID_IBufferinflate, idx, byteBuffer);
    //env_->DeleteLocalRef(byteBuffer);
    //jthrowable ex = env_->ExceptionOccurred();
    //if (ex != NULL) {
    //    env_->ExceptionDescribe();
    //    env_->ExceptionClear();
    //}
}
inline void javacodec_ibuffer_release(int idx, unsigned timestamp, int flags)
{
    //env_->CallVoidMethod(oDecoderWrap, MID_IBufferrelease, idx, timestamp, flags);
    //jthrowable ex = env_->ExceptionOccurred();
    //if (ex != NULL) {
    //    env_->ExceptionDescribe();
    //    env_->ExceptionClear();
    //}
}

int javacodec_obuffer_obtain(void** pv, unsigned* len)
{
    //int idx = env_->CallIntMethod(oDecoderWrap, MID_OBufferobtain, 50);
    //if (idx >= 0) {
    //    jobject bytebuf = env_->GetObjectField(oDecoderWrap, FID_outputBuffer);
    //    *pv = env_->GetDirectBufferAddress(bytebuf);
    //    *len = env_->GetDirectBufferCapacity(bytebuf);
    //    env_->DeleteLocalRef(bytebuf);
    //    _TRACE_OUT_INC(*len);
    //}
    return 0;//idx;
    //void*       (*GetDirectBufferAddress)(JNIEnv*, jobject);
    //jlong       (*GetDirectBufferCapacity)(JNIEnv*, jobject);
    //jfieldID    (*GetFieldID)(JNIEnv*, jclass, const char*, const char*);
    //jobject     (*GetObjectField)(JNIEnv*, jobject, jfieldID);
}

inline void javacodec_obuffer_release(int idx) {
    //env_->CallVoidMethod(oDecoderWrap, MID_OBufferrelease, idx, 0);
    //jthrowable ex = env_->ExceptionOccurred();
    //if (ex != NULL) {
    //    env_->ExceptionDescribe();
    //    env_->ExceptionClear();
    //}
}

inline int stage(signed char y, char const* fx) {
    std::swap(stage_, y);
    LOGD("%s %d->%d", fx, y, stage_);
    return y;
}

#if 0
struct nalu_data_sink
{
    pthread_mutex_type mutex_;
    std::deque<mbuffer> bufs_;

    //typedef boost::intrusive::slist<mbuffer,boost::intrusive::cache_last<true>> slist_type;
    //slist_type blis_, blis_ready_;

    nalu_data_sink() {
    }
    ~nalu_data_sink() {
        _TRACE_SIZE_PRINT(); /*fclose(fp_);*/
    }

    enum { Types_SDP78 = ((1<<7)|(1<<8)) };
    enum { Types_SDP67 = ((1<<6)|(1<<7)) };
    unsigned short fwd_types_ = 0;

    bool sdp_ready() const { return ((fwd_types_ & Types_SDP78) == Types_SDP78) || ((fwd_types_ & Types_SDP67) == Types_SDP67); }

    void pushbuf(mbuffer&& bp)
    {
        auto* h = bp.nal_header();
        _TRACE_NET_INCOMING(h->type, bp.end()-bp.begin());

//#define DONOT_DROP 0
        if (h->nri < 3)/*(0)*/ {
            //_TRACE_DROP0(1);
            return;
        }
        if (sdp_ready()) {
            if (h->type > 5) {
                //fwrite((char*)bp.addr(-4), 4+bp.size, 1, fp_);
                return;
            }
        }

        pthread_mutex_lock_guard lkguard(mutex_); //std::lock_guard<std::mutex> lock(mutex_);

        if (!bufs_.empty() && sdp_ready())/*(0)*/ {
            //_TRACE_DROP1(bufs_.size());
            bufs_.clear(); //if (bufs_.size() > 4) bufs_.pop_front();
        }
        bufs_.push_back(std::move(bp));
    }
    //void commit(rtp_header const& rh, uint8_t* data, uint8_t* end) { pushbuf(mbuffer(rh, data, end)); }

    void jcodec_inflate()
    {
        _TRACE_PRINT_RATE();
        pthread_mutex_lock_guard lkguard(mutex_); //std::lock_guard<std::mutex> lock(mutex_);
        while (!bufs_.empty() /*&& (tc[0].ns.dec - tc[0].ns.out) < 3 *//*&& !(tc[0].ns.nfr&0x400)*/) { // TODO:testing
            int idx = javacodec_ibuffer_obtain(15);
            if (idx < 0) {
                //LOGW("buffer obtain: %d", idx);
                break;
            }

            mbuffer buf = std::move(bufs_.front());
            bufs_.pop_front();
            auto* h = buf.nal_header();

            int flags = 0;
            switch (h->type) {
                case 0: case 7: case 8: flags = BUFFER_FLAG_CODEC_CONFIG; break;
            }
            javacodec_ibuffer_inflate(idx, (char*)buf.begin_startbytes(), buf.end()-buf.begin_startbytes());
            javacodec_ibuffer_release(idx, 1, flags);

            if (!sdp_ready()) {
                fwd_types_ |= (1<<h->type);
                LOGD("sdp %02x", fwd_types_);
            }
            _TRACE_DEC_INC(h->nri, h->type, buf.end()-buf.begin());
        }
    }

    //static slist_type::iterator iterator_before(slist_type& blis, mbuffer& b)
    //{
    //    auto it = blis.before_begin();
    //    auto p = it++;
    //    for (; it != blis.end(); p=it++) {
    //        if (it.operator->() == &b)
    //            break;
    //    }
    //    return p;
    //}
};
#endif

JNIEXPORT int VideoFrameDecoded::_query(void**data, unsigned* size) {
    //if (sink_) {
    //    sink_->jcodec_inflate();
    //    if (sink_->sdp_ready()) {
    //        return javacodec_obuffer_obtain(data, size);
    //    }
    //}
    return -1;
}
JNIEXPORT void VideoFrameDecoded::_release(int idx) {
    //javacodec_obuffer_release(idx);
}

//JNIEXPORT int hgs_JNI_OnLoad(JavaVM* vm, void*)
//{
//    //jvm_ = vm;
//    //JNIEnv* env;
//    //if (vm->GetEnv((void**) &env, JNI_VERSION_1_4) != JNI_OK)
//    //    return -1;
//    //char const* clsName = "com/huazhen/barcode/engine/DecoderWrap";
//    //jclass cls = env->FindClass(clsName);//CHECK(cls); //=env->GetObjectClass(oDecoderWrap);
//    //CLS_DecoderWrap = (jclass)env->NewGlobalRef(cls);
//
//    //MID_IBufferobtain  = env->GetMethodID(cls, "cIBufferObtain" , "(I)I");
//    //MID_IBufferinflate = env->GetMethodID(cls, "cIBufferInflate", "(ILjava/nio/ByteBuffer;)V");
//    //MID_IBufferrelease = env->GetMethodID(cls, "cIBufferRelease", "(III)V");
//
//    //MID_OBufferobtain  = env->GetMethodID(cls, "cOBufferObtain" , "(I)I");
//    //MID_OBufferrelease = env->GetMethodID(cls, "cOBufferRelease", "(II)V");
//    //FID_outputBuffer   = env->GetFieldID (cls, "outputBuffer"   , "Ljava/nio/ByteBuffer;");
//
//    //MID_DecoderWrap_close = env->GetMethodID(cls, "close" , "()V");
//    //// java/lang/String android/view/Surface
//    //MID_DecoderWrap_ctor = env->GetMethodID(cls, "<init>","(IILandroid/view/Surface;)V");
//
//    //LOGD("%d:%s %p %s: %p", __LINE__,__func__, env, clsName, cls);
//    return JNI_VERSION_1_4;
//}

//JNIEXPORT JNIEnv* hgs_AttachCurrentThread()
//{
//    JNIEnv*env_ = 0;
//    //jvm_->AttachCurrentThread(&env_, 0);
//    //LOGD("%d:%s %p %p", __LINE__,__func__, jvm_, env_);
//    //if (!env_) {
//    //    LOGE("AttachCurrentThread");
//    //}
//    return env_;
//}
//JNIEXPORT void hgs_DetachCurrentThread()
//{
//    //if (jvm_->DetachCurrentThread() != JNI_OK) {
//    //    LOGE("DetachCurrentThread");
//    //}
//    //LOGD("%d:%s %p %p", __LINE__,__func__, jvm_, env_);
//}

//JNIEXPORT void* hgs_init(int w, int h, jobject surface)
JNIEXPORT bool hgs_init( jint w, jint h, ANativeWindow* window)
{
    LOGD("window %p", window);
    tmpargs_.reset( new Main::Args() );
    tmpargs_->vWidth = w;
    tmpargs_->vHeight = h;

    if (window) {
        //ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
        tmpargs_->renderer_ok_ = 1;
        tmpargs_->vgl.setWindow(window); // pass Ownership // ANativeWindow_release
    }
    //jobject o = env_->NewObject(CLS_DecoderWrap, MID_DecoderWrap_ctor, w,h, surface);
    //oDecoderWrap = env_->NewGlobalRef(o);
    //env_->DeleteLocalRef(o);
    //LOGD("%d:%s %dx%d %p: %p", __LINE__,__func__, w,h,surface, oDecoderWrap);
    //return (void*)oDecoderWrap;
    return 1;
}

JNIEXPORT int hgs_start(char const* ip, int port, char const* path)
{
    LOGD("%s:%d %s", ip, port, path);
    //BOOST_ASSERT(oDecoderWrap);
    stage(1, __func__);

    int retval = 0;
#   if defined(TEST_WITH_VIDEO_FILE)
    test_.reset( new TestMain() );
    test_->thread.start();
#   else
    char uri[256];
    if (port == 554) {
        snprintf(uri,sizeof(uri), "rtsp://%s%s", ip, path);
    } else {
        snprintf(uri,sizeof(uri), "rtsp://%s:%d%s", ip, (int)port, path);
    }
    hgsnwk_init(ip, port, uri);

    //const char *ip = env->GetStringUTFChars(js_ip, 0);
    //env->ReleaseStringUTFChars(js_ip, ip);

    //{}/*=*/{
    //    nalu_data_sink* sink = new nalu_data_sink();
    //    sink_.reset(sink);
    //    hgs_run([sink](mbuffer b){ sink->pushbuf(std::move(b)); });
    //}
    hgsnwk_run( nwk_dataincoming );
#   endif
    LOGD("stage %d", stage_);
    return retval;
}

JNIEXPORT void hgs_stop()
{
    LOGD("stage %d", stage_);
    if (stage_ > 0) {
        stage(0, __func__);
#   if defined(TEST_WITH_VIDEO_FILE)
        test_->thread.stop();
        test_.reset();
#   else
        hgsnwk_exit(1);
        hgsnwk_exit(0);
        _TRACE_RESET();
#   endif
    }
    main_->thread.stop();
    main_.reset();
}

#ifdef ENABLE_TEST_JNICALL

extern "C" JNIEXPORT void JNICALL
Java_com_huazhen_barcode_MainActivity_JNIinitDecoder(JNIEnv *env, jclass cls, jint w, jint h, jobject surface) {
    ANativeWindow* window = 0;
    if (surface) {
        window = ANativeWindow_fromSurface(env, surface); // ANativeWindow_release
        ERR_EXIT_IF(!window, "ANativeWindow_fromSurface: %p %p", env, surface);
    }
    hgs_init( w,h, window);
}
extern "C" JNIEXPORT void JNICALL
Java_com_huazhen_barcode_MainActivity_JNIstart(JNIEnv *env, jclass, jstring js_ip, jint port, jstring js_path) {
    const char *ip = env->GetStringUTFChars(js_ip, 0);
    const char *path = env->GetStringUTFChars(js_path, 0);

    hgs_start(ip, port, path);

    env->ReleaseStringUTFChars(js_ip, ip);
    env->ReleaseStringUTFChars(js_path, path);
}
extern "C" JNIEXPORT void JNICALL
Java_com_huazhen_barcode_MainActivity_JNIstop(JNIEnv *env, jclass) {
    hgs_stop();
}

extern "C" JNIEXPORT void JNICALL
Java_com_huazhen_barcode_MainActivity_JNIpump(JNIEnv *env, jclass)
{
    //VideoFrameDecoded d = VideoFrameDecoded::query();
    //if (!d.empty()) {
    //    ;
    //}
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved)
{
    LOGD("%s", abi_str());
#if 0
    static const char* clsName = "com/huazhen/barcode/engine/MainActivity";
    static JNINativeMethod methods[] = {
        { "hello", "(IILandroid.view.Surface;)V", (void*)hello }
    };
    enum { N_methods = sizeof(methods)/sizeof(methods[0]) };

    jclass clz = env->FindClass(clsName);
    if (!clz) {
        LOGE("FindClass %s: fail", clsName);
        return -1;
    }
    int retval = env->RegisterNatives(clz, methods, N_methods);
    if (retval < 0) {
        LOGE("RegisterNatives %s: fail", clsName);
        return -1;
    }
    LOGD("%s: %d", __func__, retval);
#endif
    //return hgs_JNI_OnLoad(vm, reserved);
    return JNI_VERSION_1_4;
}
//extern "C" JNIEXPORT jint JNICALL JNI_UnLoad(JavaVM* vm, void* reserved)

#endif

#ifdef ENABLE_TEST_MAIN
int main(int argc, char* const argv[])
{
    hgs_init(1920,1080, 0);
    hgs_start("192.168.0.1", 554, argc>1?argv[1]:"/live/ch00_2");
    while (1)
        sleep(5);
    hgs_stop();
    LOGD("bye.");
    return 0;
}
#endif

