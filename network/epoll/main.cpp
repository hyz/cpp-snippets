#include <boost/algorithm/string.hpp>
#include "buffer.hpp"
#include "epoll.hpp"
//#include "enc.hpp"

enum { BITSTREAM_LEN  = (1920 * 1080 * 3 / 2) }; // (720 * 576 * 3 / 2)
typedef buffer_list_fix<BITSTREAM_LEN> buffer_list;

static char* trim_right(char* s) {
    char* e = s + strlen(s);
    while (e > s && isspace(*(e-1)))
        --e;
    *e = '\0';
    return s;
}

static FILE* open_rec_file()
{
    //char filename[50] = "/mnt/nfs/tmp/rec.264";
    //sprintf(filename, "/mnt/nfs/tmp/ch%d_%dx%d.264", cch_ , gm_system.cap[cch_].dim.width, gm_system.cap[cch_].dim.height);
    return NULL;//fopen(filename, "wb");
}

int main(int argc, char* const argv[])
{
    buffer_list buflis;
    //Encoder<buffer_list> enc(buflis, open_rec_file());
    NetworkIO<buffer_list> nwk(buflis, argc>1 ? argv[1] : NULL);

    //enc.thread.start(); // run
    nwk.thread.start(); // run

    //DEBUG("'/help'");
    for (;;) {
        char line[512];
        fgets(line,sizeof(line), stdin);

        if (trim_right(line)[0] == '\0')
            continue;
        if (line[0] == '/') {
            if (strcmp(line,"/help") == 0) {
            } else if (strcmp(line,"/exit") == 0 || strcmp(line,"/quit") == 0) {
                //enc.thread.stop();
                nwk.thread.stop();
                break;
            }
        } else {
            strcat(line, "\n");
            unsigned len = strlen(line);
            buffer_list::iterator it = buflis.alloc();
            /*_*/{
                uint32_t u4 = htonl(len);
                it->put((char*)&u4, sizeof(uint32_t));
            }
            it->put(line, len);
            buflis.done(it);
        }
    }

    nwk.thread.join();
    //enc.thread.join(); // pthread_join
}

