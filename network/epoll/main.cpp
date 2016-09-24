#include <boost/algorithm/string.hpp>
#include "buffer.hpp"
#include "epoll.hpp"
#include "enc.hpp"

struct timeval extimeval_;

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

struct PadInfo {
    uint32_t u4;
    uint16_t ts[2];

    PadInfo() {}
    PadInfo(unsigned x, struct timeval tv) {
        u4 = x;
        ts[0] = short(tv.tv_sec % 10000);
        ts[1] = short(tv.tv_usec / 1000);
    }
    unsigned millis() const { return ts[0]*1000u + ts[1]; }
};

enum { Port=9990 };

struct ServerMain : boost::noncopyable
{
    typedef array_buf<16> Recvbuffer;

    typedef cycle_buffer_queue<malloc_buf<BITSTREAM_LEN>,2> cycle_list;
    struct BufferQueue : cycle_list {
        void done(iterator j) {
            if (j == ialloc_) {
                replace_startbytes_with_len4(j->begin(), j->size());
                my_->attach_test_info(*j);
                //if (record_file)
                //    //fwrite(bufs[i]->begin(), bufs[i]->size(), 1, record_file);
            }
            cycle_list::done(j);
        }
        ServerMain* my_;
    };
    //typedef cycle_buffer_queue<malloc_buf<BITSTREAM_LEN>,2> BufferQueue; //typedef buffer_queue_<BITSTREAM_LEN> BufferQueue;

    BufferQueue bufq;
    NetworkIO<TCPServer, ServerMain::BufferQueue&,ServerMain&> nwk; //(srv.bufq, srv, NULL);
    Thread<ServerMain> thread;
    //pthread_mutex_type mutex_;

    //enum { Feedback_packsize=16 };

    int operator()(Recvbuffer& rb, BufferQueue& q) { return recvd(rb,q); }
    int recvd(Recvbuffer& rb, BufferQueue&)
    {
        char* p = rb.begin();
        while (p + sizeof(PadInfo) <= rb.end()) {
            ERR_EXIT_IF(ptrdiff_t(p) % 4, "");
            RTT((PadInfo*)p);
            p += sizeof(PadInfo);
        }
        return int(p - rb.begin()); //rb.consume(p - rb.begin()); return 0;
    }
    void run() {
        nwk.xpoll(&thread.stopped);
    }

    ServerMain() : nwk(bufq, *this, NULL, Port), thread(*this, "ServerMain")
    {
        ofp_ = 0; //fopen("server.timestamp.log","wb");
        bufq.my_ = this;
        RTT_reset();
    }
    ~ServerMain() {
        if (ofp_ && ofp_ != stdout && ofp_ != stderr) {
            fclose(ofp_);
        }
    }

    int latency_sum, latency[0x0f+1];
    unsigned latency_i;

    void RTT(PadInfo*p) {
        PadInfo q(0, uptime::elapsed() );

        if (q.millis() < p->millis()) {
            LOGE("%u < %u", q.millis(), p->millis());
            //RTT_reset();
        } else {
            int i = (latency_i++ & 0x0f);
            int millis = latency[i];
            latency[i] = q.millis() - p->millis();
            int diff = latency[i] - millis;
            latency_sum += diff ; //latency[i] - millis;
            if ((latency_i & 0x3f) == 0x20) {
                DEBUG("%04d.%03d %d", q.ts[0], q.ts[1], latency_sum/(0x0f+1));
                //DEBUG("latency:%d:%u %3d-%-3d=%3d %4d, %d",i,p->u4, latency[i], millis, diff, latency_sum, latency_sum/0x0f);
            }
                DEBUG("%04d.%03d %d", q.ts[0], q.ts[1], latency_sum/(0x0f+1));
        }
    }
    void RTT_reset() {
        memset(latency, 0, sizeof(latency));
        latency_sum = 0;
        latency_i = 0;
    }
    FILE* ofp_;
#if 1
    static void replace_startbytes_with_len4(char* bs_buf, uint32_t bs_len4) {
        if ((ptrdiff_t)bs_buf % 4) {
            ERR_EXIT("bs_buf addr %p", bs_buf);
        }
        uint32_t* u4 = (uint32_t*)bs_buf;
        if (ntohl(*u4) != 0x00000001) {
            ERR_EXIT("bs_buf startbytes %08x", *u4);
        }
        *u4 = htonl(bs_len4);

        {
            static unsigned maxlen4 = 0;
            if (bs_len4 > maxlen4) {
                maxlen4=bs_len4;
                LOGV("max bs_len4 %u", maxlen4);
            }
        }
    }
    void attach_test_info(buffer_ref& buf) {
        static unsigned fidx = 0;
        uint32_t uh = htonl(0x80000000 | (4+sizeof(PadInfo)));
        PadInfo inf( fidx++, uptime::elapsed() );
        buf.put((char*)&uh, sizeof(uint32_t));
        buf.put((char*)&inf, sizeof(PadInfo));
    }
#endif
};

struct ClientMain : boost::noncopyable
{
    typedef malloc_buf<BITSTREAM_LEN> Recvbuffer;

    typedef cycle_buffer_queue<array_buf<16>,16> BufferQueue;
    BufferQueue bufq;
    NetworkIO<TCPClient, ClientMain::BufferQueue&,ClientMain&> nwk; //(cli.bufq, cli, argv[1]);
    Thread<ClientMain> thread;

    FILE* ofp_;
    ClientMain(char const* ip) : nwk(bufq, *this, ip, Port), thread(*this, "ClientMain") {
        ofp_ = 0;
    }

    void run() {
        nwk.xpoll(&thread.stopped);
    }

    int operator()(Recvbuffer& rb, BufferQueue& txbufs) { return recvd(rb, txbufs); }
    int recvd(Recvbuffer& rb, BufferQueue& txbufs)
    {
        ERR_EXIT_IF(&txbufs!=&bufq, "");
        if (rb.size() < 8) {
            return 0;
        }
        if ((ptrdiff_t)rb.begin() % sizeof(uint32_t)) {
            ERR_EXIT("process_recvd_data %p", rb.begin());
        }

        char* const p = rb.begin();
            uint32_t uh = ntohl( *(uint32_t*)p );
        char* const end = p + (uh & 0x0fffffff);

        DEBUG("size %u %u", end-p, rb.size());
        if (rb.end() < end) {
            return 0;
        }

        if ((uh & 0x80000000)) {
            feedback(txbufs, p+4, end);

        } else {
            *(uint32_t*)p = htonl(0x00000001);

            //DEBUG("");//("h:size %u %u", lenp4, rb.size());
            if (ofp_) {
                fwrite(p, end-p, 1, ofp_);
            }
        }
        return int(end - p); //(endp - p);
    }

    void feedback(BufferQueue& txbufs, char* p, char* end) {
        BufferQueue::iterator it = txbufs.alloc(end - p);
        it->put(p, end-p); //((char*)&inf, sizeof(inf));
        txbufs.done(it);
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
            continue;
        }

        if (bufq) {
#ifdef NOENC
            unsigned len = strlen( strcat(line, "\n") );
            typename Queue::iterator it = bufq->alloc(512);
            /*_*/{
                uint32_t u4 = htonl(0x00000001);//(len);
                it->put((char*)&u4, sizeof(uint32_t));
            }
            it->put(line, len);
            bufq->done(it);
#endif
        } else {
            DEBUG("### console input ignored.");
        }
    }
}

static void second_clock()
{
    while (1) {
        struct timeval tv = uptime::elapsed();
        printf("\r%04u.%03u  ", unsigned(tv.tv_sec%10000), unsigned(tv.tv_usec/1000));
        fflush(stdout);

        tv.tv_sec = 0;
        tv.tv_usec = 10*1000;
        select(0,NULL,NULL,NULL,&tv);
    }
}

int main(int argc, char* const argv[])
{
    gettimeofday(&extimeval_,NULL); //clock_gettime(CLOCK_REALTIME, &tv);

    DEBUG("bs_len %u, errno %s", (int)BITSTREAM_LEN, strerror(errno));
    if (argc == 2) { // client
        ClientMain cli(argv[1]);//( stdout );
        cli.thread.start(); // run

        //second_clock();
    user_input((ClientMain::BufferQueue*)NULL);//(&bufq);

        cli.thread.stop();
        cli.thread.join();

    } else { // server
        ServerMain srv;//(fopen("tmp/feedback.out","wb"));
        Encoder<ServerMain::BufferQueue&> enc(srv.bufq);

        srv.thread.start(); // run
        enc.thread.start(); // run

        user_input(&srv.bufq);//((ServerMain::BufferQueue*)NULL); //(&srv.bufq);
        //second_clock();

        enc.thread.stop();
        srv.thread.stop();
        enc.thread.join(); // pthread_join
        srv.thread.join();
    }
}


