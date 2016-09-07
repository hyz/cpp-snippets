#include <boost/algorithm/string.hpp>
#include "buffer.hpp"
#include "epoll.hpp"
#include "enc.hpp"

typedef buffer_list_fix<BITSTREAM_LEN> BufferList;

static char* trim_right(char* s) {
    char* e = s + strlen(s);
    while (e > s && isspace(*(e-1)))
        --e;
    *e = '\0';
    return s;
}

//char filename[50] = "/mnt/nfs/tmp/rec.264";
//sprintf(filename, "/mnt/nfs/tmp/ch%d_%dx%d.264", cch_ , gm_system.cap[cch_].dim.width, gm_system.cap[cch_].dim.height);

struct FileSink
{
    enum { max_packet_size=BITSTREAM_LEN };

    void operator()(char* p, unsigned len) {
        if (ofp)
            fwrite(p, len, 1, ofp);
    }
    FileSink(FILE* f) {
        ofp = f;
    }
    ~FileSink() {
        if (ofp && ofp != stdout && ofp != stderr) {
            fclose(ofp);
        }
    }
    FILE* ofp;
};

void user_input(BufferList* buflis)
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
        } else if (buflis) {
            strcat(line, "\n");
            unsigned len = strlen(line);
            BufferList::iterator it = buflis->alloc(512);
            /*_*/{
                uint32_t u4 = htonl(len);
                it->put((char*)&u4, sizeof(uint32_t));
            }
            it->put(line, len);
            buflis->done(it);
        }
    }
}

static BufferList buflis;

int main(int argc, char* const argv[])
{
    DEBUG("errno %s", strerror(errno));
    if (argc == 2) { // client
        FileSink rec264( fopen("/sdcard/tmp/rec2.264","wb") );

        NetworkIO<BufferList&,FileSink&> nwk(buflis, rec264, argv[1]);
        nwk.thread.start(); // run

    user_input(NULL);//(&buflis);
        nwk.thread.stop();
        nwk.thread.join();

    } else { // server
        FileSink recnwk( fopen("tmp/feedback.out","wb") );
        FileSink recenc(NULL);//( fopen("tmp/rec1.264","wb") );

        Encoder<BufferList> enc(buflis, recenc.ofp);
        NetworkIO<BufferList&,FileSink&> nwk(buflis, recnwk, NULL);
        enc.thread.start(); // run
        nwk.thread.start(); // run

    user_input(NULL);//(&buflis);
        enc.thread.stop();
        nwk.thread.stop();
        nwk.thread.join();
        enc.thread.join(); // pthread_join
    }
}

