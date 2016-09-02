#include <boost/algorithm/string.hpp>
#include "buffer.hpp"
#include "epoll.hpp"
//#include "enc.hpp"
typedef buffer_list_fix<1024> buffer_list;

static char* trim_right(char* s) {
    char* e = s + strlen(s);
    while (e > s && isspace(*(e-1)))
        --e;
    *e = '\0';
    return s;
}

int main(int argc, char* const argv[])
{
    buffer_list buflis;
    //Encoder enc(buflis);
    EPollContext<buffer_list> nwk(buflis, argc>1 ? argv[1] : NULL);

    //enc.thread.start(); // run
    nwk.thread.start(); // run

    //DEBUG("Enter y to force keyframe, q to quit\n");
    for (buffer_list::iterator it = buflis.begin(); ; ) {
        char line[512];
        fgets(line,sizeof(line), stdin);

        if (trim_right(line)[0] == '\0')
            continue;
        if (line[0] == '/') {
            if (strcmp(line,"/exit") == 0 || strcmp(line,"/quit") == 0) {
                //enc.thread.stop();
                nwk.thread.stop();
                break;
            }
        } else {
            strcat(line, "\n");
            it = buflis.alloc(it);
            it->put(line, strlen(line));
            buflis.done(it);
        }
    }

    nwk.thread.join();
    //enc.thread.join(); // pthread_join(enc_thread_id, NULL);

}

