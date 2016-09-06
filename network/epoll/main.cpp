#include <boost/algorithm/string.hpp>
#include "buffer.hpp"
#include "epoll.hpp"
#include "enc.hpp"

typedef buffer_list_fix<BITSTREAM_LEN> buffer_list;

static char* trim_right(char* s) {
    char* e = s + strlen(s);
    while (e > s && isspace(*(e-1)))
        --e;
    *e = '\0';
    return s;
}

//char filename[50] = "/mnt/nfs/tmp/rec.264";
//sprintf(filename, "/mnt/nfs/tmp/ch%d_%dx%d.264", cch_ , gm_system.cap[cch_].dim.width, gm_system.cap[cch_].dim.height);

struct Sink
{
    enum { max_packet_size=BITSTREAM_LEN };

    void operator()(char* p, unsigned len) {
        if (ofp)
            fwrite(p, len, 1, ofp);
    }
    Sink(FILE* f) {
        ofp = f;
        if (!ofp)
            ofp = stdout;
    }
    ~Sink() {
        if (ofp && ofp != stdout && ofp != stderr) {
            fclose(ofp);
        }
    }
    FILE* ofp;
};

typedef buffer_list BufferList;

void user_input(BufferList& buflis)
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
        } else {
            strcat(line, "\n");
            unsigned len = strlen(line);
            BufferList::iterator it = buflis.alloc(512);
            /*_*/{
                uint32_t u4 = htonl(len);
                it->put((char*)&u4, sizeof(uint32_t));
            }
            it->put(line, len);
            buflis.done(it);
        }
    }
}

int main(int argc, char* const argv[])
{
    DEBUG("errno %s", strerror(errno));
    if (argc == 2) { // client
        Sink rec264( fopen("tmp/rec2.264","wb") );
        BufferList dummyLis;

        NetworkIO<BufferList,Sink> nwk(dummyLis, rec264, argv[1]);
        nwk.thread.start(); // run

        user_input(dummyLis);
        nwk.thread.stop();
        nwk.thread.join();

    } else {
        Sink recnwk( fopen("tmp/feedback.out","wb") );
        Sink recenc( fopen("tmp/rec1.264","wb") );
        BufferList buflis;

        Encoder<BufferList> enc(buflis, recenc.ofp);
        NetworkIO<BufferList,Sink> nwk(buflis, recnwk, NULL);
        enc.thread.start(); // run
        nwk.thread.start(); // run

        user_input(buflis);
        enc.thread.stop();
        nwk.thread.stop();
        nwk.thread.join();
        enc.thread.join(); // pthread_join
    }
}

#if defined(__arm__) && (__GNUC__ == 4) && (__GNUC_MINOR__ == 4)
# warning "__arm__ gcc4.4"
#endif

