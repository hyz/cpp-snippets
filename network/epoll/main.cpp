#include <boost/algorithm/string.hpp>
#include "buffer.hpp"
#include "epoll.hpp"
#include "enc.hpp"

static char* trim_right(char* s) {
    char* e = s + strlen(s);
    while (e > s && isspace(*(e-1)))
        --e;
    *e = '\0';
    return s;
}

//char filename[50] = "/mnt/nfs/tmp/rec.264";
//sprintf(filename, "/mnt/nfs/tmp/ch%d_%dx%d.264", cch_ , gm_system.cap[cch_].dim.width, gm_system.cap[cch_].dim.height);

typedef clock_realtime_type Clock;

struct ServerMain
{
    typedef array_buf<16> Recvbuffer;

    typedef cycle_buffer_queue<malloc_buf<BITSTREAM_LEN>,2> cycle_list;
    struct BufferQueue : cycle_list {
        void done(iterator j) {
            if (j == ialloc_) {
                replace_startbytes_with_len4(j->begin(), j->size() - 4);
                mysrv_->attach_test_info(*j);
            }
            return cycle_list::done(j);
        }
        ServerMain* mysrv_;
    };
    //typedef cycle_buffer_queue<malloc_buf<BITSTREAM_LEN>,2> BufferQueue; //typedef buffer_queue_<BITSTREAM_LEN> BufferQueue;

    BufferQueue bufq;
    enum { Feedback_packsize=16 };
    pthread_mutex_type mutex_;

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
    void attach_test_info(buffer_ref& buf) {
        enum { Test_Tail_Len=8 };
        static unsigned fidx = 0;
        uint32_t v[2];
        v[0] = fidx++;
        v[1] = milliseconds(Clock::now() - Clock::midnight());
        //unsigned mnight = milliseconds(Clock::midnight() - Clock::epoch());
        //unsigned now = milliseconds(Clock::now() - Clock::epoch());
        if (ofp_) {
            pthread_mutex_lock_guard lk(mutex_);
            fprintf(ofp_,"%u %u %u\n", v[0], v[1], (int)buf.size()); //("%u %u, now-midnight %u-%u=%u", v[0], v[1], now, mnight, now-mnight);
        }
        buf.put((char*)&v, sizeof(v));
    }

    int operator()(Recvbuffer& rb, BufferQueue&) {
        while (rb.size() >= Feedback_packsize) {
            char* p = rb.begin();
            uint32_t* v = (uint32_t*)p; // [0, seq, timestamp, size]
            if ((ptrdiff_t)p % sizeof(uint32_t) || (void*)v != (void*)p)
                ERR_EXIT("process_recvd_data %p %p", p, v);

            uint32_t mnight = milliseconds(Clock::now() - Clock::midnight());
            if (ofp_) {
                pthread_mutex_lock_guard lk(mutex_);
                fprintf(ofp_,"%u RTT %u-%u=%d %u\n", v[0], mnight, v[1], int(mnight - v[1]), v[2]);
            }

            rb.consume(Feedback_packsize);
        }
        return 0;
    }
    ServerMain(FILE*) {
        ofp_ = fopen("server.timestamp.log","wb");
        bufq.mysrv_ = this;
    }
    ~ServerMain() {
        if (ofp_ && ofp_ != stdout && ofp_ != stderr) {
            fclose(ofp_);
        }
    }
    FILE* ofp_;
};

struct ClientMain
{
    typedef malloc_buf<BITSTREAM_LEN> Recvbuffer;

    typedef cycle_buffer_queue<array_buf<16>,16> BufferQueue;
    BufferQueue bufq;
    enum { Feedback_Tail_Len=8 };

    FILE* ofp_;
    ClientMain(FILE* fp) : ofp_(fp) {}

    int operator()(Recvbuffer& rb, BufferQueue& txbufs) {
        ERR_EXIT_IF(&txbufs!=&bufq, "");
        while (rb.size() >= sizeof(uint32_t)) {
            char* p = rb.begin();
            uint32_t* u4 = (uint32_t*)p;
            if ((ptrdiff_t)p % sizeof(uint32_t) || (void*)u4 != (void*)p)
                ERR_EXIT("process_recvd_data %p %p", p, u4);

            unsigned lenp4 = 4 + ntohl(*u4);
            if (rb.size() < lenp4 + Feedback_Tail_Len) {
                return 0;
            }
            *u4 = htonl(0x00000001);

            feedback(txbufs, p+lenp4, Feedback_Tail_Len, lenp4);
            //DEBUG("");//("h:size %u %u", lenp4, rb.size());
            if (ofp_) {
                fwrite(p, lenp4, 1, ofp_);
            }

            rb.consume(lenp4 + Feedback_Tail_Len);
        }
        return 0;
    }

    void feedback(BufferQueue& txbufs, char* p, unsigned, unsigned lenp4) {
        ERR_EXIT_IF(Feedback_Tail_Len!=8, "");
        uint32_t v[4]; //// [ seq, timestamp, size, 0]
        memcpy(&v[0], p, Feedback_Tail_Len);
        v[2] = lenp4;

        BufferQueue::iterator it = txbufs.alloc(sizeof(v));
        it->put((char*)&v, sizeof(v));
        txbufs.done(it);

        unsigned mnight = milliseconds(Clock::now() - Clock::midnight());
        DEBUG("%u RTT %u-%u=%d %u", v[0], mnight, v[1], int(mnight - v[1]), v[2]);
    }

};

template <typename Queue> void user_input(Queue* bufq)
{
    //DEBUG("'/help'");
    for (;;) {
        char line[512];
        fgets(line,sizeof(line), stdin);

        if (trim_right(line)[0] == '\0')
            continue;
        if (line[0] == '/') {
            if (strcmp(line,"/help") == 0) {
            } else if (strcmp(line,"/exit") == 0 || strcmp(line,"/quit") == 0) {
                break;
            }
        } else if (bufq) {
            strcat(line, "\n");
            unsigned len = strlen(line);
            typename Queue::iterator it = bufq->alloc(512);
            /*_*/{
                uint32_t u4 = htonl(len);
                it->put((char*)&u4, sizeof(uint32_t));
            }
            it->put(line, len);
            bufq->done(it);
        } else {
            DEBUG("### console input ignored.");
        }
    }
}

int main(int argc, char* const argv[])
{
    DEBUG("errno %s", strerror(errno));
    if (argc == 2) { // client
#ifdef NOENC
        ClientMain cli(NULL);//( stdout );
#else
        ClientMain cli(NULL);//( fopen("/sdcard/tmp/rec2.264","wb") );
#endif
        NetworkIO<ClientMain::BufferQueue&,ClientMain&> nwk(cli.bufq, cli, argv[1]);
        nwk.thread.start(); // run

    user_input((ClientMain::BufferQueue*)0);//(&bufq);
        nwk.thread.stop();
        nwk.thread.join();

    } else { // server
#ifdef NOENC
        ServerMain srv( stdout );
#else
        ServerMain srv( fopen("tmp/feedback.out","wb") );
#endif
        Encoder<ServerMain::BufferQueue&> enc(srv.bufq, NULL);
        NetworkIO<ServerMain::BufferQueue&,ServerMain&> nwk(srv.bufq, srv, NULL);
        enc.thread.start(); // run
        nwk.thread.start(); // run

#ifdef NOENC
    user_input(&srv.bufq);
#else
    for (;;) sleep(3); //user_input((ServerMain::BufferQueue*)0);//(&bufq);
#endif
        enc.thread.stop();
        nwk.thread.stop();
        nwk.thread.join();
        enc.thread.join(); // pthread_join
    }
}

