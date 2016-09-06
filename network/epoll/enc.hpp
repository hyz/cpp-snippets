#ifndef ENC_CPP__
#define ENC_CPP__

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <sys/time.h>
#include "log.hpp"
#include "thread.hpp"
#include "clock.hpp"
#include "epoll.hpp"
#include "buffer.hpp"
#ifndef NOENC
# include "gmlib.h"
#endif

enum { BITSTREAM_LEN  = (1920 * 1080 * 3 / 2) }; // (720 * 576 * 3 / 2)

template <typename BufferList>
struct Encoder : boost::noncopyable
{
    enum { MAX_BITSTREAM_NUM = 1 };

    Thread<Encoder> thread;
    BufferList& buflis;
#ifndef NOENC
    gm_system_t gm_system;
    void *capture_object;
    void *encode_object;
    enum { cch_ = 0 }; //cch_ = 0; // use capture virtual channel 0

    FILE *record_file;

    ~Encoder() {
        gm_delete_obj(encode_object);
        gm_delete_obj(capture_object);
        gm_release();
        /// fclose(record_file);
    }
#endif
    Encoder(BufferList& lis, FILE* recfile=0)
        : thread(*this, "Encode")
        , buflis(lis)
    {
#ifndef NOENC
        DECLARE_ATTR(cap_attr, gm_cap_attr_t);
        DECLARE_ATTR(h264e_attr, gm_h264e_attr_t);

        gm_init(); //gmlib initial(GM????ʼ??)
        gm_get_sysinfo(&gm_system);
        
        capture_object = gm_new_obj(GM_CAP_OBJECT); // new capture object(??ȡ??׽????)
        encode_object = gm_new_obj(GM_ENCODER_OBJECT);  // // create encoder object (??ȡ????????)
        
        cap_attr.cap_vch = cch_;
        
        //GM8210 capture path 0(liveview), 1(CVBS), 2(can scaling), 3(can scaling)
        //GM8139/GM8287 capture path 0(liveview), 1(CVBS), 2(can scaling), 3(can't scaling down)
        cap_attr.path = 3;
        cap_attr.enable_mv_data = 0;
        gm_set_attr(capture_object, &cap_attr); // set capture attribute (???ò?׽????)

        h264e_attr.dim.width = gm_system.cap[cch_].dim.width;
        h264e_attr.dim.height = gm_system.cap[cch_].dim.height;
        h264e_attr.frame_info.framerate = gm_system.cap[cch_].framerate;
        h264e_attr.ratectl.mode = GM_CBR;
        h264e_attr.ratectl.gop = 1; //60;
        h264e_attr.ratectl.bitrate = 8192;  // 2Mbps
        h264e_attr.b_frame_num = 0; // B-frames per GOP (H.264 high profile)
        h264e_attr.enable_mv_data = 0;  // disable H.264 motion data output
        gm_set_attr(encode_object, &h264e_attr);

        record_file = recfile; //stdout; //fopen(filename, "wb");
#endif
    }
    void run() // thread func
    {
#ifndef NOENC
        void *groupfd = gm_new_groupfd(); // create new record group fd (??ȡgroupfd)
        void *bindfd = gm_bind(groupfd, capture_object, encode_object);
        if (gm_apply(groupfd) < 0) {
            ERR_EXIT("Error! gm_apply fail, AP procedure something wrong!");
        }

        gm_pollfd_t poll_fds[MAX_BITSTREAM_NUM];
        gm_enc_multi_bitstream_t multi_bs[MAX_BITSTREAM_NUM];
        typename BufferList::iterator bufs[MAX_BITSTREAM_NUM];

        memset(poll_fds, 0, sizeof(poll_fds));
        for (int i = 0; i < MAX_BITSTREAM_NUM; i++) {
            bufs[i] = buflis.alloc(BITSTREAM_LEN);
        }

        poll_fds[cch_].bindfd = bindfd;
        poll_fds[cch_].event = GM_POLL_READ;
        while (!thread.stopped) {
            /** poll bitstream until 500ms timeout */
            int ret = gm_poll(poll_fds, MAX_BITSTREAM_NUM, 500);
            if (ret == GM_TIMEOUT) {
                DEBUG("Poll timeout");
                continue;
            }

            memset(multi_bs, 0, sizeof(multi_bs));  //clear all mutli bs
            for (int i = 0; i < MAX_BITSTREAM_NUM; i++) {
                if (poll_fds[i].revent.event != GM_POLL_READ)
                    continue;
                if (poll_fds[i].revent.bs_len > BITSTREAM_LEN) {
                    ERR_MSG("bitstream buffer length is not enough %d, %d\n",
                        poll_fds[i].revent.bs_len, BITSTREAM_LEN);
                    continue;
                }
                multi_bs[i].bindfd = poll_fds[i].bindfd;
                multi_bs[i].bs.bs_buf = bufs[i]->begin();//bitstream_data;  // set buffer point(ָ?????����?ָ??λ??)
                multi_bs[i].bs.bs_buf_len = BITSTREAM_LEN;  // set buffer length(ָ?????峤??)
                multi_bs[i].bs.mv_buf = 0;  // not to recevie MV data
                multi_bs[i].bs.mv_buf_len = 0;  // not to recevie MV data
            }
            
            ret = gm_recv_multi_bitstreams(multi_bs, MAX_BITSTREAM_NUM);
            if (ret < 0) {
                ERR_MSG("Error return value %d", ret);
            } else {
                for (int i = 0; i < MAX_BITSTREAM_NUM; i++) {
                    if ((multi_bs[i].retval < 0) && multi_bs[i].bindfd) {
                        ERR_MSG("CH%d Error to receive bitstream. ret=%d", i, multi_bs[i].retval);
                    } else if (multi_bs[i].retval == GM_SUCCESS) {
                        //DEBUG("<CH%d, mv_len=%d bs_len=%d, keyframe=%d, newbsflag=0x%x>",
                        //    i, multi_bs[i].bs.mv_len, multi_bs[i].bs.bs_len, multi_bs[i].bs.keyframe, multi_bs[i].bs.newbs_flag);
                        if (record_file) {
                            fwrite(multi_bs[i].bs.bs_buf, multi_bs[i].bs.bs_len, 1, record_file);
                            fflush(record_file);
                        }

                        replace_startbytes_with_len4(multi_bs[i].bs.bs_buf, multi_bs[i].bs.bs_len - 4);
                        bufs[i]->commit(*bufs[i], multi_bs[i].bs.bs_len);
                        buflis.done(bufs[i]);
                        bufs[i] = buflis.alloc(BITSTREAM_LEN);
                    }
                }
            }
        }
        gm_unbind(bindfd);
        gm_apply(groupfd);
        gm_delete_groupfd(groupfd);
#endif
    }

    static void replace_startbytes_with_len4(char* bs_buf, uint32_t bs_len4) {
        if ((ptrdiff_t)bs_buf % 4) {
            ERR_EXIT("bs_buf addr %p", bs_buf);
        }
        uint32_t* u4 = (uint32_t*)bs_buf;
        if (ntohl(*u4) != 0x00000001)
            ERR_EXIT("bs_buf startbytes %08x", *u4);
        *u4 = htonl(bs_len4);

        {
            static unsigned maxlen4 = 0;
            if (bs_len4 > maxlen4) {
                maxlen4=bs_len4;
                fprintf(stderr, "max bs_len4 %u\n", maxlen4);
            }
        }
    }
};

#endif // ENC_CPP__

