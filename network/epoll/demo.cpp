#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <sys/time.h>
#include "gmlib.h"
#include "log.hpp"
#include "thread.hpp"
#include "buffer.hpp"
#include "clock.hpp"
#include "epoll.hpp"

enum { BITSTREAM_LEN  = (1920 * 1080 * 3 / 2) }; // (720 * 576 * 3 / 2)
typedef FixBuffers<BITSTREAM_LEN> buffer_list;

struct Encoder : boost::noncopyable
{
    enum { MAX_BITSTREAM_NUM = 1 };

    Thread<Encoder> thread;
    gm_system_t gm_system;
    void *capture_object;
    void *encode_object;
    enum { cch_ = 0 }; //cch_ = 0; // use capture virtual channel 0

    buffer_list& buflis;
    //FILE *record_file;

    ~Encoder() {
        gm_delete_obj(encode_object);
        gm_delete_obj(capture_object);
        gm_release();
        //fclose(record_file);
    }

    Encoder(buffer_list& lis)
        : thread(*this, "Encoder")
        , buflis(lis)
    {
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

        //record_file = stdout; //fopen(filename, "wb");
        //char filename[50];
        //sprintf(filename, "ch%d_%dx%d_force.264", cch_
        //        , gm_system.cap[cch_].dim.width, gm_system.cap[cch_].dim.height);
        //fopen(filename, "wb");
        //if (record_file == NULL) {
        //    ERR_EXIT("open '%s' error", filename);
        //}
    }
    void run() //(void *arg)
    {
        void *groupfd = gm_new_groupfd(); // create new record group fd (??ȡgroupfd)
        void *bindfd = gm_bind(groupfd, capture_object, encode_object);
        if (gm_apply(groupfd) < 0) {
            ERR_EXIT("Error! gm_apply fail, AP procedure something wrong!");
        }

        gm_pollfd_t poll_fds[MAX_BITSTREAM_NUM];
        gm_enc_multi_bitstream_t multi_bs[MAX_BITSTREAM_NUM];
        buffer_list::iterator bufs[MAX_BITSTREAM_NUM];

        memset(poll_fds, 0, sizeof(poll_fds));
        for (int i = 0; i < MAX_BITSTREAM_NUM; i++) {
            bufs[i] = buflis.alloc(buflis.begin());
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
                        buflis.done(bufs[i]);
                        bufs[i] = buflis.alloc(bufs[i]);
                        DEBUG("<CH%d, mv_len=%d bs_len=%d, keyframe=%d, newbsflag=0x%x>",
                                i, multi_bs[i].bs.mv_len, multi_bs[i].bs.bs_len,
                                multi_bs[i].bs.keyframe, multi_bs[i].bs.newbs_flag);
                        //fwrite(multi_bs[i].bs.bs_buf, 1, multi_bs[i].bs.bs_len, record_file);
                        //fflush(record_file);
                    }
                }
            }
        }
        gm_unbind(bindfd);
        gm_apply(groupfd);
        gm_delete_groupfd(groupfd);
    }
};

int main(int argc, char* const argv[])
{
    buffer_list buflis;
    Encoder enc(buflis);
    EPollContext<buffer_list> nwk(buflis);
    //Sender send;

    enc.thread.start();
    nwk.thread.start();
    //send.thread.start();

    //DEBUG("Enter y to force keyframe, q to quit\n");
    while (1) {
    //int key;
    //    key = getchar();
    //    if (key == 'y') {
    //        DEBUG("Force keyframe\n");
    //        gm_request_keyframe(bindfd);
    //    } else if(key == 'q') {
            // enc.thread.stop();
            // nwk.thread.stop();
    //        break;
    //    }
        sleep(9);
    }

    nwk.thread.join();
    enc.thread.join(); // pthread_join(enc_thread_id, NULL);
}

