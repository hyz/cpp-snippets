#ifndef EPOLL_HPP__
#define EPOLL_HPP__

#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
//#include <list>
#include <vector>
#include "log.hpp"
#include "thread.hpp"
#include "buffer.hpp"

inline struct sockaddr_in make_sa(char const*ip, short port)
{
    struct sockaddr_in sa;
    bzero(&sa, sizeof(sa));
    sa.sin_family = AF_INET;
    if (ip) {
        if (inet_pton(AF_INET, ip, &sa.sin_addr) < 0) {
            ERR_EXIT("inet_pton");
        }
    } else {
        sa.sin_addr.s_addr = INADDR_ANY;
    }
    sa.sin_port = htons(port);
    return sa;
}

template <typename Context>
struct EventInterface {
    virtual ~EventInterface() {}
    virtual void on_events(Context& ctx, int evts) = 0;
};

struct epoll_socket_access_helper {
    template <typename T> static T* init(T& o, int fd, struct sockaddr_in&) {
        o.fd_=fd;
        o.events_ = 0;
        return o.nonblocking(); // return &o;
    }
};
template <typename XData, int Type, typename Context>
struct EPollSocket : EventInterface<Context>, boost::noncopyable
{
    typedef EPollSocket<XData,Type,Context> ThisType;
    typedef XData XDataType;
    /// EPOLLIN | EPOLLOUT | EPOLLET
    // enum { type=Type; }

    ~EPollSocket() {
        if (fd_ >= 0)
            ::close(fd_);
    }
    EPollSocket() { fd_=-1; }
    EPollSocket(XData& xd) : xdata_(xd) { fd_=-1; events_=0; }
    EPollSocket(int xfd, XData& xd) : xdata_(xd) { fd_ = xfd; events_=0; }

    XData& xdata() { return xdata_; }
    int type() const { return Type; }

    int ok(int msk) const { return (events_ & msk); } // bool
    int fd() const { return fd_; }
    bool is_open() const { return fd_>=0; }
    void close() {
        if (is_open()) {
            ::close(fd_);
            fd_ = -1;
            events_ = 0;
        }
    }

    int recv(char*p, char*end) {
        return recv_(p,end, NULL);
    }
    int send(char*p, char*end) {
        return send_(p,end, NULL, 0);
    }
    int recv(char*p, char*end, struct sockaddr_in& sa) {
        return recv_(p,end, &sa);
    }
    int send(char*p, char*end, struct sockaddr_in& sa) {
        return send_(p,end, &sa, sizeof(sa));
    }

    int connect(const char* ip, unsigned short port) {
        struct sockaddr_in sa = make_sa(ip,port);
        open();
        int ec = ::connect(fd_, (struct sockaddr *)&sa, sizeof(sa));
        if (ec < 0) {
            if (errno == EINPROGRESS) {
                DEBUG("connect %s:%d EINPROGRESS", ip, (int)port);
            } else {
                ERR_EXIT("connect");
            }
        } else {
            DEBUG("connected %s:%d [OK]", ip, (int)port);
        }
        return ec;
    }
    int bind(const char* ip, unsigned short port) {
        struct sockaddr_in sa = make_sa(ip,port);
        open();
        int rc = ::bind(fd_, (struct sockaddr *)&sa, sizeof(sa));
        if (rc < 0) {
            ERR_EXIT("bind");
        }
        DEBUG("bind %s:%d", ip ? ip : "*", (int)port);
        return rc;
    }
    int listen(const char* ip, unsigned short port, int backlog=128) {
        int y=1;
        open();
        if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &y, sizeof(y)) < 0) {
            ERR_EXIT("setsockopt");
        }
        int rc;
        rc = this->bind(ip,port);
        if (rc < 0) {
            ERR_EXIT("bind");
        }
        rc = ::listen(fd_, backlog);
        if (rc < 0) {
            ERR_EXIT("listen");
        }
        return rc;
    }
    template <typename Sock> int accept(Sock& sk) {
        struct sockaddr_in sa;
        socklen_t slen = sizeof(struct sockaddr_in);
        int afd = ::accept(fd_, (struct sockaddr *)&sa, &slen);
        if (afd < 0) {
            events_ = 0;
            if (errno == EAGAIN)
                return 0;
            ERR_EXIT("accept: %d", afd);
        }
        epoll_socket_access_helper::init(sk, afd, sa);

        {
            char tmp[64]; // = {0};
            inet_ntop(AF_INET, &sa.sin_addr, tmp,64);
            DEBUG("accept: %s", tmp);
        }
        return 1;
    }

//protected:
    void init_with_fd(int fd) {
        if (fd_ >= 0)
            ERR_EXIT("init_with_fd");
        events_ = 0;
        fd_ = fd;
    }

    ThisType* fcntl(int msk, bool yn)
    {
        int flags = ::fcntl(fd_, F_GETFL);
        if (flags < 0) {
            ERR_EXIT("fcntl %d F_GETFL", fd_);
        }
        int prevflags = flags;
        if (yn) {
            flags |= msk; // | O_NONBLOCK;
        } else {
            flags &= ~msk;
        }
        if (flags != prevflags) {
            if(::fcntl(fd_, F_SETFL, flags) < 0) {
                ERR_EXIT("fcntl %d F_SETFL", fd_);
            }
        }
        return this;
    }
    ThisType* nonblocking() { return fcntl(O_NONBLOCK, true); }

    ThisType* open() {
        if (is_open()) {
            return this;
        }
        int sfd = ::socket(AF_INET, Type, 0);
        if (sfd < 0)
            ERR_EXIT("socket:SOCK_DGRAM");
        events_ = 0;
        fd_ = sfd;
        return this;
    }

    friend struct epoll_socket_access_helper;
    int fd_;
    uint32_t events_;
    XData xdata_; //array_buf<bufsiz> xdata_;

    int recv_(char*p, char*end, struct sockaddr_in* sa) {
        if (p>=end || !ok(EPOLLIN)) {
            ERR_EXIT("recv error: %d %d", int(end-p), int(events_ & EPOLLIN));
        }
        socklen_t slen = sizeof(struct sockaddr_in);
        int n = ::recvfrom(fd_, p, end-p, 0, (struct sockaddr*)sa, sa?&slen:NULL);
        if (n <= 0) //(n < (int)s.size())
            events_ &= ~EPOLLIN;
        if (n < 0) {
            if (errno == EAGAIN) {
                return 0;
            }
            ERR_MSG("recv");
            return n;
        }
        if (n == 0) {
            return -int(errno = EPIPE);
        }
        return n;
    }
    int send_(char* p, char* end, struct sockaddr_in* sa, socklen_t slen) {
        if (p>=end || !ok(EPOLLOUT)) {
            ERR_EXIT("send error: %d %d", int(end-p), int(events_ & EPOLLOUT));
        }
        enum { MTU=1500-64 };
        int xlen = std::min(int(MTU), int(end-p));
        int n = ::sendto(fd_, p, xlen, 0, (struct sockaddr*)sa, slen);
        if (n < xlen)
            events_ &= ~EPOLLOUT;
        if (n < 0) {
            if (errno == EAGAIN)
                return 0;
            ERR_MSG("send");
            return n;
        }
        return n;
    }

private:
    virtual void on_events(Context& ctx, int evts) {
        //DEBUG("process_events events %x, %d", evts, (int)ok(EPOLLOUT));
        events_ = evts;
        ctx.process_io(*this);
    }
};

struct EPoll : boost::noncopyable
{
    ~EPoll() {
        if (epfd >= 0)
            ::close(epfd);
    }
    EPoll() : epfd( create_() ) {}

    int wait(struct epoll_event *events, int maxevents, int timeout) {
        return ::epoll_wait(epfd, events, maxevents, timeout);
    }

    template <typename T>
    void add(T& sk, int evts) {
        struct epoll_event ev;
        ev.data.ptr = to_data_ptr_(&sk);
        ev.events = evts|EPOLLET; //EPOLLIN|EPOLLOUT | EPOLLET;
        if (::epoll_ctl(epfd, EPOLL_CTL_ADD, sk.fd(), &ev) < 0)
            ERR_EXIT("epoll_ctl");
    }

    template <typename T>
    void del(T& sk) {
        struct epoll_event ev;
        if (epoll_ctl(epfd, EPOLL_CTL_DEL, sk.fd(), &ev) < 0)
            ERR_EXIT("epoll_ctl");
    }

    // EPOLL_CTL_MOD

private:
    int epfd; 

    static int create_() {
        int fd = ::epoll_create(16);
        if (fd < 0)
            ERR_EXIT("epoll_create");
        return fd;
    }
    template <typename T> inline void* to_data_ptr_(EventInterface<T>* p) { return static_cast<void*>(p); }
};

template <typename Context, typename ListenSocket>
struct Listen_service
{
    Context* context;
    //if (sk.type() == SOCK_STREAM) {
        //if (sk.state() == EnumConnecting) {
        //    int y;
        //    if (::getsockopt(lisk.fd(), SOL_SOCKET, SO_ERROR, &y, sizeof(y)) < 0) {
        //        ERR_EXIT("getsockopt");
        //    }
        //    if (y != 0) {
        //        return error(sk); //ERR_EXIT("getsockopt SO_ERROR %d", y);
        //    }
        //}
    //}

    void process_io(ListenSocket& lisk) {
        typedef typename ListenSocket::XDataType StreamSocket;
        StreamSocket& tmpsk = lisk.xdata();
        while (lisk.ok(EPOLLIN)) {
            int n = lisk.accept(tmpsk);
            if (n <= 0) {
                if (n < 0)
                    ERR_EXIT("accept");
                DEBUG("accept:EINPROGRESS");
            } else {
                StreamSocket* newsk = context->new_connection(/*std::move*/tmpsk);
                if (newsk) {
                    context->epoll.add(*newsk, EPOLLIN|EPOLLOUT);
                } else {
                    ERR_MSG("new_connection");
                    tmpsk.close();
                }
            }
        }
    }

    void setup(ListenSocket& lisk, const char* ip, unsigned short port) {
        if (lisk.listen(ip, port) < 0) {
            ERR_EXIT("listen");
        }
        if (lisk.fcntl(O_NONBLOCK, true) < 0) {
            ERR_EXIT("fcntl");
        }
        context->epoll.add(lisk, EPOLLIN);
    }

    Listen_service(Context& c) :context(&c) {}
};

template <typename Context, typename StreamSocket>
struct Stream_service
{
    Context* context;

    void process_io(StreamSocket& sk) {
        if (sk.ok(EPOLLIN)) {
            return context->do_recv(sk);
        }
        if (sk.ok(EPOLLOUT)) {
            DEBUG("EPOLLOUT");
            return context->do_send(sk);
        }
    }

    void setup(StreamSocket& sk) {
        if (sk.fcntl(O_NONBLOCK, true) < 0) {
            ERR_EXIT("listen:fcntl");
        }
        context->epoll.add(sk, EPOLLOUT|EPOLLIN);
    }

    Stream_service(Context& c) :context(&c) {}
};

template <typename Buffer_list>
struct NetworkIO //: IO_objects<Buffer_list>
{
    //typedef IO_objects<Buffer_list> Base;
    typedef NetworkIO<Buffer_list> This;
    typedef array_buf<1024*24-64> Recvbuffer;

    typedef EPollSocket<Recvbuffer  ,SOCK_STREAM,This> StreamSocket;
    typedef EPollSocket<StreamSocket,SOCK_STREAM,This> ListenSocket;
    typedef EPollSocket<Recvbuffer  ,SOCK_DGRAM ,This> DatagramSocket;

    Buffer_list& buflis;
    typename Buffer_list::iterator iter_; // = buflis.end();

    //DatagramSocket udp_;
    ListenSocket lisk_;
    Listen_service<This,ListenSocket> listenio_;
    StreamSocket streamsk_;
    Stream_service<This,StreamSocket> streamio_;

    EPoll epoll;
    Thread<NetworkIO> thread;

    ~NetworkIO() {
        do_close(lisk_);
        do_close(streamsk_);
    }

    NetworkIO(Buffer_list& lis, char const* connect_ip = 0, short port=9990)
        : buflis(lis)
        //, udp_(), streamsk_()
        , listenio_(*this)
        , streamio_(*this)
        , thread(*this, "NetworkIO")
    {
        iter_ = buflis.end();
#if 1
        if (connect_ip) {
            streamsk_.connect(connect_ip, port);
            streamio_.setup(streamsk_);
        } else {
            listenio_.setup(lisk_, NULL, port);
        }
#else
        if (connect_ip) {
            datagramio_.connect(connect_ip, port);
        } else {
            datagramio_.bind(NULL, port);
        }
            epoll.add(udp_, EPOLLIN|EPOLLOUT);
#endif
    }

    void run() // thread func
    {
        while (!thread.stopped) {
            if (iter_ == buflis.end()) {
                iter_ = buflis.wait(10);
            }
            if (iter_ != buflis.end()) {
                do_send(streamsk_); // do_recv(udp_);
            }

            struct epoll_event evts[16];
            int nready = epoll.wait(evts, sizeof(evts)/sizeof(*evts), 50);
            if (nready < 0) {
                ERR_EXIT("epoll_wait");
            }

            for (int i = 0; i < nready; ++i) {
                EventInterface<This>* xif = static_cast<EventInterface<This>*>(evts[i].data.ptr);
                xif->on_events(*this, evts[i].events);
            }
        }
    }
    // std::tuple<>
    void process_io(ListenSocket& lisk) { listenio_.process_io(lisk); }
    void process_io(StreamSocket& sk) { streamio_.process_io(sk); }

    StreamSocket* new_connection(/*std::move*/StreamSocket& sk) {
        if (&sk != &streamsk_) {
            streamsk_.init_with_fd(sk.fd());
            sk.fd_ = -1;
        }
        return &streamsk_;
    }

    template <typename Sock> void error(Sock& sk, int ec) {
        ERR_MSG("error %d", sk.fd());
        do_close(sk);
    }
    template <typename Sock> void do_close(Sock& sk) {
        if (sk.is_open()) {
            ERR_MSG("close %d", sk.fd());
            epoll.del(sk);
            sk.close();
        }
    }

    void process_recvd_data(Recvbuffer& rb) {
        while (rb.size() >= sizeof(uint32_t)) {
            char* p = rb.begin();
            uint32_t* u4 = (uint32_t*)p;
            if ((ptrdiff_t)p % sizeof(uint32_t) || (void*)u4 != (void*)p)
                ERR_EXIT("process_recvd_data %p %p", p, u4);

            unsigned len = ntohl(*u4);
            if (rb.size() < len+4) {
                break;
            }
            *u4 = htonl(0x00000001);

            {
                p += 4;
                //len -= sizeof(uint32_t);
                fwrite(p, len, 1, stdout);
            }

            rb.consume(4+len);
        }
    }

    template <typename Sock> void do_recv(Sock& sk) {
        while (sk.ok(EPOLLIN)) {
            Recvbuffer& rb = sk.xdata();
            Recvbuffer::range sp = rb.spaces();
            int n = sk.recv(sp.begin(), sp.end());//(, sa_peer_);
            if (n < 0) {
                return error(sk, errno);
            }
            if (n > 0) {
                rb.commit(sp, n);
                process_recvd_data(rb);
            }
        }
    }
    template <typename Sock> void do_send(Sock& sk) {
        while (sk.ok(EPOLLOUT) && iter_ != buflis.end()) {
            int n = sk.send(iter_->begin(), iter_->end());//(sa_peer_);
            if (n < 0) {
                return error(sk, errno);
            }
            iter_->consume(n);
            if (iter_->empty()) {
                buflis.done(iter_);
                iter_ = buflis.end();
            }
        }
    }
};

template <typename Context, typename DatagramSocket>
struct IO_datagram
{
    Context* context;
    bool established_;

    IO_datagram& send(DatagramSocket& sk) {
        if (established()) {
            while (sk.ok(EPOLLIN)) {
                ;
            }
            while (sk.ok(EPOLLOUT)) {
                ;
            }
            //if (iter_ == buflis.end())
            //    break;
            //int n = sk.send(iter_->begin(), iter_->end());//(sa_peer_);
            //if (n < 0) {
            //    return error(sk, errno);
            //}
            //iter_->consume(n);
            //if (iter_->empty()) {
            //    buflis.done(iter_);
            //    iter_ = buflis.end();
            //}
        } else {
            if (sk.ok(EPOLLIN)) {
                ;
            }
        }
        return *this;
    }

    bool established() const { return established_; }

    void connect(DatagramSocket& sk, const char* ip, unsigned short port) {
        if (sk.connect(ip,port) < 0) {
            ERR_EXIT("connect");
        } else {
            established_ = 1;
            //sa_peer_ = make_sa(ip,port);
            sk.fcntl(O_NONBLOCK, true);
            context->epoll_add(sk, EPOLLIN|EPOLLOUT);
        }
    }

    void bind(DatagramSocket& sk, const char* ip, unsigned short port) {
        if (sk.bind(ip,port) < 0) {
            ERR_EXIT("bind");
        } else {
            established_ = 0;
            //sa_peer_ = make_sa(ip,port);
            sk.fcntl(O_NONBLOCK, true);
            context->epoll_add(sk, EPOLLIN|EPOLLOUT);
        }
    }

    IO_datagram(Context& c) :context(&c) {}
};

struct NetworkMain {
};

#endif // EPOLL_HPP__

