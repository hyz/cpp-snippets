#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h> 
#include <netdb.h> 
#include <stdint.h> 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <exception>
#include <algorithm>
#include <boost/noncopyable.hpp>

inline void exit127_(int,int) { exit(127); }
#define ERR_EXIT(...) exit127_( fprintf(stderr, __VA_ARGS__), fprintf(stderr, ": %d:%s error:%d\n", errno,strerror(errno),__LINE__) )
#define ERR_MSG(...) (void)( fprintf(stderr, __VA_ARGS__), fprintf(stderr, ": %d:%s error:%d\n", errno,strerror(errno),__LINE__) )
#define DBG_MSG(...) (void)( fprintf(stderr, __VA_ARGS__), fprintf(stderr, " debug:%d\n",__LINE__) )

static unsigned sMills(struct timeval const& tv0) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (1000*1000*(tv.tv_sec - tv0.tv_sec) + tv.tv_usec - tv0.tv_usec)/1000;
}
static unsigned sMills() {
    static struct timeval tv0 = {};
    if (tv0.tv_sec == 0 && tv0.tv_usec == 0)
        gettimeofday(&tv0, NULL);
    return sMills(tv0);
}

typedef struct sockaddr SA;
static const socklen_t SOCKLEN_ = sizeof(struct sockaddr_in);

static void sockaddr_init(struct sockaddr_in* sa, char const* ip, short port)
{
    bzero(sa, sizeof(struct sockaddr_in));
    sa->sin_family = AF_INET;
    inet_pton(AF_INET, ip, &sa->sin_addr);
    sa->sin_port = htons( port );
}
static struct sockaddr_in sockaddr_init(char const* ip, short port)
{
    struct sockaddr_in sa;
    sockaddr_init(&sa, ip, port);
    return sa;
}

struct TCPAcceptor {
    int socket_;
    TCPAcceptor(char const* bind_ip, short bind_port) {
        socket_ = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in sa;
        sockaddr_init(&sa, bind_ip, bind_port);
        if (bind(socket_, (SA*)&sa, SOCKLEN_) < 0)
            ERR_EXIT("bind %s:%d", bind_ip, bind_port);
        if (listen(socket_, 96) < 0)
            ERR_EXIT("listen %s:%d", bind_ip, bind_port);
    }
    ~TCPAcceptor() { close(socket_); }

    int accept(struct sockaddr_in *sa) {
        socklen_t slen = sizeof(struct sockaddr_in);
        return ::accept(socket_, (SA*)sa, &slen);
    }
    int accept() {
        struct sockaddr_in sa;
        return this->accept(&sa);
    }
};
int tcp_connect(char const* dst_ip, short dst_port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa;
    sockaddr_init(&sa, dst_ip, dst_port);
    if (connect(sockfd, (SA*)&sa, SOCKLEN_) < 0)
        ERR_EXIT("connect %s:%d", dst_ip, dst_port);
    return sockfd;
}
int udp_bind(char const* ip, short port)
{
    struct sockaddr_in sa;
    sockaddr_init(&sa,ip, port);
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (bind(sockfd, (SA*)&sa, SOCKLEN_) < 0) {
        ERR_EXIT("bind %s:%d", ip, port);
    }
    return sockfd;
}
int udp_connect(char const* ip, short port)
{
    struct sockaddr_in saddr;
    socklen_t slen = sizeof(struct sockaddr_in);
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    sockaddr_init(&saddr,ip, port);
    if (connect(sockfd, (SA*)&saddr, slen) < 0) {
        ERR_EXIT("connect %s:%d", ip, port);
    }
    return sockfd;
}

template <typename Proto>
struct Main : boost::noncopyable
{
    Proto proto_;
    Main(int sfd, int Nps, int Kps) //: io_(tcp, dst_ip, dst_port)
    {
        socket_ = sfd;

        bzero(&sent_, sizeof(sent_));
        bzero(&recv_, sizeof(recv_));

        wmills_ = 1000/(Nps);
        bytes1_ = std::min( std::max(Kps*1024/Nps, 1024*4), 1024*56 );
        npf_ = nif_ = 0;

        runc_ = 0x7fffffff;
        run_ = &Main::do_send;
    }
    Main(int sfd)
    {
        socket_ = sfd;

        bzero(&sent_, sizeof(sent_));
        bzero(&recv_, sizeof(recv_));

        runc_ = 0x7fffffff;
        run_ = &Main::do_receive;
    }
    ~Main() { ::close(socket_); }

    void run() {
        srand(time(0));
        while ( runc_-- > 0 ) {
            (this->*run_)();
        }
    }

private:
    struct recv_st
    {
        struct timeval tv0_;

        unsigned seq;
        unsigned n_dgram;
        unsigned bytes_recvd;
        unsigned bytes_exped;
        unsigned millsec;
        unsigned millsec_local;

        void incoming(unsigned bytes_recvd, unsigned len, unsigned seq, unsigned mills) {
			if (n_dgram == 0)
				gettimeofday(&tv0_, NULL);
            recv_st prev = *this;

            this->n_dgram++;
            this->bytes_exped += len;
            this->seq = seq;
            this->bytes_recvd += bytes_recvd;
            this->millsec = mills;
            this->millsec_local = sMills();

            this->print(prev);
        }

        void print(recv_st const& prev) const {
            char const* seqv = 0;
            if (seq != prev.seq+1) {
                char const* legv[3] = { "misorder", "repeated", "lost" };
                seqv = legv[(seq<prev.seq ? -1 : (seq==prev.seq ? 0 : 1)) +1];
            }

            unsigned ct = sMills(tv0_);
            if (seqv || ((ct - tpr_) > 5 && (seq&0x7f)==0) || (seq&0x7ff) == 0 || seq<3) {
                int nsec = (ct)/1000;
                if (nsec == 0)
                    nsec = 1;
                printf("%3u.%03u %4u %-4u %-4u %6.1f %7.1f %u%c", ct/1000,ct%1000
                        , prev.seq, seq, seq+1-n_dgram, float(n_dgram)/nsec, bytes_recvd/1024.0/nsec, bytes_recvd, seqv?' ':'\n');
                if (seqv)
                    printf("* %s\n", seqv);//("* %u a %u\n", bytes_recvd-prev.bytes_recvd, bytes_recvd);
                tpr_=ct;
            }
        }
        
        mutable unsigned tpr_;
    } recv_;

    struct sent_st {
        struct timeval tv0_;
        unsigned seq;
        unsigned bytes_sent;

        void outgoing(unsigned, unsigned bytes) {
            if (seq == 0) {
				gettimeofday(&tv0_, NULL);
            }
            sent_st const& prev = *this;
            seq++;
            bytes_sent += bytes;
            this->print(prev);
        }
        //unsigned numb_psec_ = 30;
        //unsigned bytes_psec_ = 1024*300;

        void print(sent_st const&) const {
            unsigned ct = sMills(tv0_);
            int nsec = ct/1000;
            if (nsec == 0)
                nsec = 1;
            if (((ct - tpr_) > 5 && (seq&0x7f)==0) || (seq&0x7ff) == 0 || seq < 3) {
                printf("%3u.%03u %4u %4.1f %4.1f %u\n", ct/1000,ct%1000, seq, float(seq+1)/nsec, bytes_sent/1024.0/nsec, bytes_sent);
                tpr_ = ct;
            }
        }

        mutable unsigned tpr_;
    } sent_;

private:
    short npf_, nif_;
    short wmills_;
    unsigned short bytes1_;

    int socket_;
    enum { max_length = 1024*80 };
    int32_t data_[max_length/4];

    void do_receive()
    {
        int len = proto_.recv(socket_, data_, sizeof(data_));
        if (len < 20) {
            ERR_EXIT("recv %d", len);
        }
        recv_.incoming(len, ntohl(data_[0]), ntohl(data_[1]), ntohl(data_[2]));
    }

    void do_send()
    {
        if (nif_ <= 0 && npf_ <= 0) {
            nif_ = 3+rand()%3;
            npf_ = 1000/wmills_ * (100 + rand() % 200) / 100;
            //DBG_MSG("IP/frame %d %d", nif_, npf_);
        }

        int wmills = this->wmills_;
        unsigned length = bytes1_;

        if (nif_ > 0) {
            if (--nif_ == 0)
                length += (rand()%1000 - 1500);
            wmills = 0;
        } else if (npf_ > 0) {
            npf_--;
            length += (rand()%3000 - 1500);
        }
        if (length > 1024*56) {
            DBG_MSG("%u > 56K", length/1024);
            length = std::min(56*1024, int(length));
        } else if (length < 20) {
            ERR_MSG("%u", length);
        }

        data_[0] = htonl(length);
        data_[1] = htonl(sent_.seq);
        data_[2] = htonl(sMills());

        //fd_set fds; //FD_ZERO(&fds); //FD_SET(socket_, &fds);
        int retval = proto_.send(socket_, data_, length);
        if (retval < 0) 
            ERR_EXIT("sendto");
        sent_.outgoing(sent_.seq, length);

        if (wmills > 0) {
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 1000 * wmills;
            select(0, NULL, NULL, NULL, &tv);
            //std::this_thread::sleep_for(std::chrono::milliseconds(mils_idle_));
        }
    }

private:
    unsigned runc_; // = 0x7fffffff;
    void (Main::*run_)();
};

struct ProtocolTCP {
    unsigned n_read_;
    int recv(int sfd, int32_t* buf, unsigned len) {
        int nread = ::recv(sfd, &buf[0], 20, MSG_WAITALL);
        ++n_read_;
        //printf("[%d]%p %d %d: %d\n", __LINE__, &buf[0], ntohl(buf[0]), ntohl(buf[1]), nread);
        if (nread != 20)
            ERR_EXIT("recv %d", nread);
        int lenv = ntohl(buf[0]);
        if (lenv > int(len) || lenv <= 20)
            ERR_EXIT("%d %u", lenv, len);
        char* p = (char*)buf;
        int n;
        while ( (n = ::recv(sfd, p+nread, lenv-nread, MSG_WAITALL)) > 0 ) {
            ++n_read_;
            if ((nread += n) == lenv) {
                return nread;
            }
        }
        return n;
        //printf("[%d]%p %d: %d\n", __LINE__, &buf[5], lenv-20, nread);
    }
    int send(int sfd, int32_t* buf, unsigned len) {
        int n, nsend = 0;
        while ( (n = ::send(sfd, &buf[nsend], len-nsend, 0)) > 0)
            if ( (nsend += n) == len)
                return nsend;
        //printf("[%d]%p %d=%d %d: %d\n", __LINE__, &buf[0], len, ntohl(buf[0]), ntohl(buf[1]), nsend);
        return n;
    }
    ProtocolTCP() { n_read_ = 0; }
};
struct ProtocolUDP {
    int recv(int sfd, int32_t* buf, unsigned len) {
        return ::recv(sfd, buf, len, 0);
    }
    int send(int sfd, int32_t* buf, unsigned len) {
        return ::send(sfd, buf, len, 0);
    }
};

int main(int argc, char* argv[])
{
    bool prototcp = !!strstr(argv[0], "tcp");
    try {
        if (argc == 5) {
            printf("%s sending ...\n", argv[0]);
            if (prototcp) {
                Main<ProtocolTCP> sender( tcp_connect(argv[1],atoi(argv[2]))
                        , atoi(argv[3]), atoi(argv[4]));
                sender.run();
            } else {
                Main<ProtocolUDP> sender(udp_connect(argv[1],atoi(argv[2]))
                        , atoi(argv[3]), atoi(argv[4]));
                sender.run();
            }
        } else if (argc == 2) {
            printf("%s receiving ...\n", argv[0]);
            if (prototcp) {
                TCPAcceptor a("0", atoi(argv[1]));
                Main<ProtocolTCP> receiver( a.accept() );
                receiver.run();
            } else {
                Main<ProtocolUDP> receiver( udp_bind("0", atoi(argv[1])) );
                receiver.run();
            }
        } else {
            fprintf(stderr,"Usage:\n"
                    "  send:\ta.out <host> <port> <Npkg/s> <KB/s>\n"
                    "  recv:\ta.out <port>\n");
            exit(1);
        }
    } catch (std::exception const& e) {
        ERR_EXIT("Exception: %s", e.what());
    }

    return 0;
}

