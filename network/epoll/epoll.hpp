#ifndef EPOLL_HPP__
#define EPOLL_HPP__

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
//#include <list>
#include <vector>
#include <boost/assert.hpp>
#include "alog.hpp"
#include "clock.hpp"
#include "thread.hpp"
#include "buffer.hpp"

template <typename Context>
struct EventInterface {
    virtual ~EventInterface() {}
    virtual void on_events(Context& ctx, int evts) = 0;
};

struct epoll_socket_access_helper {
    template <typename T> static T* init(T& o, int fd, struct sockaddr_in&) {
        o.fd_=fd;
        o.events_ = 0;
        return o.tcp_nodelay()->nonblocking(); // return &o;
    }
};

struct Empty {};
template <int Type, typename Context, typename XData=Empty>
struct EPollSocket : EventInterface<Context>, boost::noncopyable
{
    typedef EPollSocket<Type,Context,XData> ThisType;
    typedef XData XDataType;
    // enum { type=Type; }

    ~EPollSocket() {
        if (fd_ >= 0) {
            ::close(fd_);
            DEBUG(":close %d", fd_);
        }
    }
    EPollSocket(int ofd=-1) {
        fd_=ofd;
        events_=0;
    }
    //EPollSocket(std::openmode) {
    //    fd_=open();
    //    events_=0;
    //}

    XData& xdata() { return xdata_; }
    int type() const { return Type; }

    int ok(int msk) const { return (events_ & msk); } // bool
    unsigned events() const { return events_; }

    int fd() const { return fd_; }
    bool is_open() const { return fd_>=0; }
    void close() {
        if (is_open()) {
            ::close(fd_);
            fd_ = -1;
            events_ = 0;
        }
    }

    int recv(char*p, unsigned len) { return recv_(p,len, NULL); }
    int recv(char*p, unsigned len, struct sockaddr_in& sa) { return recv_(p,len, &sa); }
    int send(char const*p, unsigned len) { return send_(p,len, NULL, 0); }
    int send(char const*p, unsigned len, struct sockaddr_in& sa) { return send_(p,len, &sa, sizeof(sa)); }

    int connect(const char* ip, unsigned short port) {
        struct sockaddr_in sa = make_sa(ip,port);
        int ec = ::connect(fd_, (struct sockaddr *)&sa, sizeof(sa));
        if (ec < 0) {
            if (errno == EINPROGRESS) {
                DEBUG("connect %s:%d EINPROGRESS", ip, (int)port);
            } else {
                ERR_EXIT("connect");
            }
        } else {
            LOGV("connect %s:%d [OK]", ip, (int)port);
        }
        return ec;
    }
    int bind(const char* ip, unsigned short port) {
        struct sockaddr_in sa = make_sa(ip,port);
        int rc = ::bind(fd_, (struct sockaddr *)&sa, sizeof(sa));
        if (rc < 0) {
            ERR_EXIT("bind");
        }
        DEBUG("%s:%d", ip, (int)port);
        return rc;
    }
    int listen(const char* ip, unsigned short port, int backlog=128) {
        int y=1;
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
    template <typename... A>
    ThisType* ioctl(int req, A... a) {
        if (::ioctl(fd, req, a...) < 0)
            ERR_EXIT("%d", fd_);
        return this;
        //static const int nonblocking = 1;
        //::ioctl(sock.fd, FIONBIO, &nonblocking); // ioctl_list FIONBIO
    }

    ThisType* nonblocking() { return fcntl(O_NONBLOCK, true); }
    ThisType* tcp_nodelay() {
        int on=1;
        if (::setsockopt(fd_, IPPROTO_TCP,TCP_NODELAY, (void*)&on,sizeof(on)) < 0)
            ERR_EXIT("%d", fd_);
        return this;
    }

    ThisType* open() {
        ERR_EXIT_IF(is_open(), "already open"); //return this;
        int sfd = ::socket(AF_INET, Type, 0);
        if (sfd < 0)
            ERR_EXIT("socket:SOCK_DGRAM");
        events_ = 0;
        fd_ = sfd;
        DEBUG(":socket %d", fd_);
        return this;
    }

    EPollSocket(EPollSocket&& rhs) : xdata_(std::move(rhs.xdata_)) {
        fd_ = rhs.fd_; rhs.fd_ = -1;
        events_ = rhs.events_; rhs.events_ = 0;
    }
    EPollSocket& operator=(EPollSocket&& rhs) {
        if (this != &rhs) {
            if (fd_ >= 0) {
                ::close(fd_);
            }
            fd_ = rhs.fd_; rhs.fd_ = -1;
            events_ = rhs.events_; rhs.events_ = 0;
            xdata_ = std::move(rhs.xdata_);
        }
        return *this;
    }

    static struct sockaddr_in make_sa(char const*ip, short port)
    {
        struct sockaddr_in sa;
        bzero(&sa, sizeof(sa));
        sa.sin_family = AF_INET;
        if (!ip || *ip == '*' || *ip == '\0') {
            sa.sin_addr.s_addr = INADDR_ANY;
        } else {
            if (inet_pton(AF_INET, ip, &sa.sin_addr) < 0) {
                ERR_EXIT("inet_pton");
            }
        }
        sa.sin_port = htons(port);
        return sa;
    }
protected: //private:
    friend struct epoll_socket_access_helper;
    //struct evfd : XData {
    //    int fd;
    //    uint32_t events;
    //} xd;
    int fd_;
    uint32_t events_;
    XData xdata_; //array_buf<bufsiz> xdata_;

    int recv_(char*p, unsigned xlen, struct sockaddr_in* sa) {
        //if (p>=end || !ok(EPOLLIN)) {
        //    ERR_EXIT("recv error: %d %d", int(end-p), int(events_ & EPOLLIN));
        //}
        socklen_t slen = sizeof(struct sockaddr_in);
        int n = ::recvfrom(fd_, p, xlen, 0, (struct sockaddr*)sa, sa?&slen:NULL);
        if (n < 0) // <=
            events_ &= ~EPOLLIN;
        if (n < 0) {
            if (errno == EAGAIN) {
                return 0;
            }
            LOGE("recv");
            return n;
        }
        if (n == 0 && Type==SOCK_STREAM) {
#if defined(__arm__) && (__GNUC__ == 4) && (__GNUC_MINOR__ == 4)
            LOGE("recv:CLOSE:EPIPE");
#else
            errno = EPIPE;
            LOGE("recv:CLOSE:EPIPE");
#endif
            return -1; //int(errno = EPIPE);
        }
        return n;
    }
    int send_(char const* p, unsigned xlen, struct sockaddr_in* sa, socklen_t slen) {
        //if (p>end )/*(|| !ok(EPOLLOUT) || sa || slen>0)*/ {
        //    ERR_EXIT("send error: %d %d", int(end-p), int(events_ & EPOLLOUT));
        //}
        //enum { MTU=1500-64 }; //std::min(int(MTU), int(end-p)); // UDP
        int n = ::sendto(fd_, p, xlen, 0, (struct sockaddr*)sa, slen);
        if (n < (int)xlen)
            events_ &= ~EPOLLOUT;
        if (n < 0) {
            if (errno == EAGAIN)
                return 0;
            LOGE("send");
            //return n;
        }
        return n;
    }

private:
    virtual void on_events(Context& ctx, int evts) {
        //DEBUG("process_events events %x, %d", evts, (int)ok(EPOLLOUT));
        events_ = evts;
        ctx.process_io(*this);
    }
private:
    EPollSocket(EPollSocket&);
    EPollSocket& operator=(EPollSocket&);
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

//template <typename Context, typename ListenSocket> struct ListenIO {
//    Context* context;
//    void process_io(ListenSocket& lisk) {
//        typedef typename ListenSocket::XDataType StreamSocket;
//        StreamSocket& tmpsk = lisk.xdata();
//        while (lisk.ok(EPOLLIN)) {
//            int n = lisk.accept(tmpsk);
//            if (n <= 0) {
//                if (n < 0)
//                    ERR_EXIT("accept");
//                DEBUG("accept:EINPROGRESS");
//            } else {
//                StreamSocket* newsk = context->new_connection(/*std::move*/tmpsk);
//                if (!newsk) {
//                    LOGE("new_connection");
//                    tmpsk.close();
//                }
//            }
//        }
//    }
//    void setup(ListenSocket& lisk, const char* ip, unsigned short port) {
//        if (lisk.listen(ip, port) < 0) {
//            ERR_EXIT("listen");
//        }
//        lisk.nonblocking();
//
//        context->epoll.add(lisk, EPOLLIN);
//        DEBUG("epoll.add %d lisk", lisk.fd());
//    }
//};

template <typename Context, typename StreamSocket>
struct StreamIO
{
    void process_io(StreamSocket& sk) {
        if (sk.ok(EPOLLIN)) {
            context->do_recv(sk);
        }
        if (sk.ok(EPOLLOUT)) {
            context->do_send(sk);
        }
    }

    void setup(StreamSocket& sk) {
        sk.tcp_nodelay()->nonblocking();
        context->epoll.add(sk, EPOLLOUT|EPOLLIN);
        DEBUG("epoll.add %d streamsk", sk.fd());
    }

    Context* context;
    StreamIO(Context& c) : context(&c) {}
};

struct TCPServer {
    template <typename Context, typename XData>
    struct traits
    {
        typedef EPollSocket<SOCK_STREAM,Context, XData> streamsk_type;
        typedef EPollSocket<SOCK_STREAM,Context, streamsk_type> socket;
        struct io : StreamIO<Context,streamsk_type>
        {
            typedef socket lisk_type;
            io(Context& c, lisk_type& lisk, char const*ip, int port)
                : StreamIO<Context,streamsk_type>(c)
            {
                lisk.open();
                if (lisk.listen(ip, port) < 0) {
                    ERR_EXIT("listen");
                }
                lisk.nonblocking();
                this->context->epoll.add(lisk, EPOLLIN);
                DEBUG("epoll.add %d lisk", lisk.fd());
            }

            void process_io(lisk_type& lisk) {
                streamsk_type& sk = lisk.xdata();
                if (sk.is_open()) {
                    LOGE("already open");
                    return;
                }
                while (lisk.ok(EPOLLIN)) {
                    int n = lisk.accept(sk);
                    if (n <= 0) {
                        if (n < 0)
                            ERR_EXIT("accept");
                        DEBUG("accept:EINPROGRESS");
                    } else {
                        //ERR_EXIT_IF(sk.is_open(), "already open");
                        //sk = std::move(tmpsk);
                        StreamIO<Context,streamsk_type>::setup(sk);
                    }
                }
            }
            void process_io(streamsk_type& sk) {
                StreamIO<Context,streamsk_type>::process_io(sk);
            }

            void close(lisk_type& lisk) {
                streamsk_type& sk = lisk.xdata();
                this->context->do_close(sk);
                this->context->do_close(lisk);
            }

            void prewait(lisk_type& lisk) {
                streamsk_type& sk = lisk.xdata();
                if (sk.is_open()) {
                    this->context->trysendx(sk);
                }
            }
        };
    };
};

struct TCPClient {
    template <typename Context, typename XData>
    struct traits
    {
        typedef EPollSocket<SOCK_STREAM, Context, XData> socket;
        struct io : StreamIO<Context,socket>
        {
            io(Context& c, socket& sk, char const*ip, int port)
                : StreamIO<Context,socket>(c)
            {
                sk.open();
                sk.connect(ip, port);
                this->setup(sk);
            }
            void close(socket& sk) {
                this->context->do_close(sk);
            }

            void process_io(socket& sk) {
                StreamIO<Context,socket>::process_io(sk);
            }

            void prewait(socket& sk) {
                if (sk.is_open()) {
                    this->context->trysendx(sk);
                }
            }
        };
    };
};

template <typename Type, typename TXQueue, typename RXSink>
struct NetworkIO : boost::noncopyable
{
    typedef NetworkIO<Type, TXQueue,RXSink> This;
    typedef typename std::decay<RXSink>::type::Recvbuffer Recvbuffer;

    EPoll epoll;
    TXQueue txqueue;
    RXSink rxsink;

    typename std::decay<TXQueue>::type::iterator tx_iter_; // = txqueue.end();
    char const* tx_p_;

    typedef typename Type:: template traits<This,Recvbuffer> traits;
    typename traits::socket socket;
    typename traits::io socket_io;

    //typedef EPollSocket<SOCK_STREAM,This, Recvbuffer  > StreamSocket;
    //typedef EPollSocket<SOCK_STREAM,This, StreamSocket> ListenSocket;
    //typedef EPollSocket<SOCK_DGRAM ,This, Recvbuffer  > DatagramSocket;
    //ListenSocket lisk_;
    //ListenIO<Devived,ListenSocket> listenio_;
    //StreamSocket streamsk_;
    //StreamIO<Devived,StreamSocket> streamio_;

    ~NetworkIO() {
        DEBUG("");
        socket_io.close(socket);
    }
    NetworkIO(TXQueue& txq, RXSink& rxs, char const* ip, int port)
        : txqueue(txq) , rxsink(rxs)
        , socket_io(*this, socket, (ip?ip:"*"), port)
    {
        DEBUG("");
        tx_p_ = 0; //tx_iter_ = txqueue.end();
    }

    void xpoll(bool* stopped) // thread func
    {
        DEBUG("");
        while (!*stopped) {
            socket_io.prewait(socket);

            struct epoll_event evts[16];
            int nready = epoll.wait(evts, sizeof(evts)/sizeof(*evts), 5);
            if (nready < 0) {
                ERR_EXIT("epoll_wait");
            }

            for (int i = 0; i < nready; ++i) {
                EventInterface<This>* xif = static_cast<EventInterface<This>*>(evts[i].data.ptr);
                xif->on_events(*this, evts[i].events);
            }
        }
        DEBUG("end");
    }

    template <typename Sock> void trysendx(Sock& sk) {
        if (!tx_p_) {
            if ( (tx_iter_ = txqueue.wait(0)) != txqueue.end()) {
                tx_p_ = tx_iter_->begin();
            }
        }
        if (tx_p_) {
            do_send(sk); // do_recv(dgramsk_);
        }
    }

    //StreamSocket* new_connection(/*std::move*/StreamSocket& sk) {
    //    if (&sk == &streamsk_) {
    //        ERR_EXIT("");
    //    }
    //    if (streamsk_.is_open()) {
    //        ERR_EXIT("already open");
    //    }
    //    streamsk_ = std::move(sk); // streamsk_.init_with_fd(sk.fd()); sk.fd_ = -1; // std::move
    //    streamio_.setup(streamsk_);
    //    return &streamsk_;
    //}

    void process_recvd(Recvbuffer& rb) {
        while (!rb.empty()) {
            int len = rxsink(rb, txqueue); // fwrite(p, len4, 1, out_fp);
            if (len > 0) {
                if (len > (int)rb.size())
                    ERR_EXIT("%d %u", len, rb.size());
                rb.consume(len);
                DEBUG("consume %u", len);
            } else
                break;
        }
    }

    template <typename Sock> void error(Sock& sk, int ec, char const* ps) {
        LOGE("%s error: %d", ps, sk.fd());
        do_close(sk);
    }

    template <typename Sock> void do_close(Sock& sk) {
        if (sk.is_open()) {
            LOGE("epoll.del %d", sk.fd());
            epoll.del(sk);
            sk.close();
        }
    }
    template <typename Sock> void do_recv(Sock& sk) {
        //DEBUG("%p", &sk);
        while (sk.ok(EPOLLIN)) {
            Recvbuffer& rb = sk.xdata();
            typename Recvbuffer::range sp = rb.spaces();
            ERR_EXIT_IF(sp.empty(),"%u %u", sp.size(), rb.size());
            int xlen = sp.size();
            int n = sk.recv(sp.begin(), xlen);//(, sa_peer_);
            DEBUG("recv %u: %d", xlen, n);
            ERR_EXIT_IF(n>xlen, "");
            if (n < 0) {
                return error(sk, errno, __FUNCTION__);
            }
            if (n == 0) {
                BOOST_ASSERT(!sk.ok(EPOLLIN));
                break;
            }
            rb.commit(sp, n);
            process_recvd(rb);
        }
    }
    template <typename Sock> void do_send(Sock& sk) {
        //DEBUG("", sk.is_open(), sk.events() );
        while (sk.ok(EPOLLOUT) && tx_p_) {
            int xlen = tx_iter_->end() - tx_p_;
            int n = sk.send(tx_p_, xlen);//(sa_peer_);
            DEBUG("send %u: %d", xlen, n);
            ERR_EXIT_IF(n>xlen, "");
            if (n < 0) {
                return error(sk, errno, __FUNCTION__);
            }
            if (n == 0) {
                BOOST_ASSERT(!sk.ok(EPOLLOUT));
                // DEBUG("need EPOLLOUT");
                break;
            }
            if (n == xlen) {
                txqueue.done(tx_iter_);
                tx_p_ = 0; // tx_iter_ = txqueue.end();
            } else {
                tx_p_ += n; // tx_iter_->consume(n);
            }
        }
    }
    template <typename Sock> void do_recv(Sock& sk, struct sockaddr_in& sa) {
        while (sk.ok(EPOLLIN)) {
            Recvbuffer& rb = sk.xdata();
            typename Recvbuffer::range sp = rb.spaces();
            ERR_EXIT_IF(sp.empty(),"%u %u", sp.size(), rb.size());
            int n = sk.recv(sp.begin(), sp.size(), sa); // DEBUG("recv: %d", n);
            if (n < 0) {
                return error(sk, errno, __FUNCTION__);
            }
            if (n == 0) {
                BOOST_ASSERT(!sk.ok(EPOLLIN));
                break;
            }
            rb.commit(sp, n);
            process_recvd(rb);
        }
    }

    // bridge //std::tuple<>
    template <typename Sock> void process_io(Sock& sk) { socket_io.process_io(sk); }
};

struct UDPClient {
    template <typename Context, typename XData>
    struct traits
    {
        typedef EPollSocket<SOCK_DGRAM, Context, XData> socket;
        struct io //: StreamIO<Context,socket>
        {
            Context* context;
            io(Context& c, socket& sk, char const*ip, int port)
                : context(&c)
            {
                sk.open();
                sk.connect(ip, port);
                sk.nonblocking();
                context->epoll.add(sk, EPOLLOUT|EPOLLIN);
                DEBUG("epoll.add %d dgramsk", sk.fd());
                // sk.send("",0);
            }
            void close(socket& sk) {
                this->context->do_close(sk);
            }

            void process_io(socket& sk) {
                //StreamIO<Context,socket>::process_io(sk);
                if (sk.ok(EPOLLIN)) {
                    context->do_recv(sk);
                }
                if (sk.ok(EPOLLOUT)) {
                    context->do_send(sk);
                }
            }

            void prewait(socket& sk) {
                this->context->trysendx(sk);
            }
        };
    };
};
struct UDPServer {
    template <typename Context, typename XData>
    struct traits
    {
        typedef EPollSocket<SOCK_DGRAM, Context, XData> socket;
        struct io //: StreamIO<Context,socket>
        {
            struct sockaddr_in sa_;
            Context* context;

            io(Context& c, socket& sk, char const*ip, int port)
                : context(&c) , sa_(socket::make_sa(0,0))
            {
                sk.open();
                sk.bind(ip, port);
                sk.nonblocking();
                context->epoll.add(sk, EPOLLOUT|EPOLLIN);
                DEBUG("epoll.add %d dgramsk", sk.fd());
            }
            void close(socket& sk) {
                context->do_close(sk);
            }

            void process_io(socket& sk) {
                if (sk.ok(EPOLLIN)) {
                    context->do_recv(sk, sa_);
                }
                if (has_peer()) {
                    if (sk.ok(EPOLLOUT)) {
                        context->do_send(sk);
                    }
                }
            }

            void prewait(socket& sk) {
                if (has_peer()) {
                    context->trysendx(sk);
                }
            }
            bool has_peer() const { return bool(sa_.sin_port); }
        };
    };
};
#if 0
template <typename Context, typename StreamSocket>
struct DatagramIO
{
    Context* context;

    void process_io(StreamSocket& sk) {
        if (sk.ok(EPOLLIN)) {
            return context->do_recv(sk);
        }
        if (sk.ok(EPOLLOUT)) {
            return context->do_send(sk);
        }
    }

    void setup(StreamSocket& sk) {
        sk.tcp_nodelay()->nonblocking();
        context->epoll.add(sk, EPOLLOUT|EPOLLIN);
        DEBUG("epoll.add %d streamsk", sk.fd());
    }

    DatagramIO(Context& c) :context(&c) {}
};
template <typename Context, typename DatagramSocket>
struct DatagramIO
{
    Context* context;

    DatagramIO& send(DatagramSocket& sk) {
        if (1/*established()*/) {
            while (sk.ok(EPOLLIN)) {
                ;
            }
            while (sk.ok(EPOLLOUT)) {
                ;
            }
            //if (tx_iter_ == txqueue.end())
            //    break;
            //int n = sk.send(tx_iter_->begin(), tx_iter_->end());//(sa_peer_);
            //if (n < 0) {
            //    return error(sk, errno);
            //}
            //tx_iter_->consume(n);
            //if (tx_iter_->empty()) {
            //    txqueue.done(tx_iter_);
            //    tx_iter_ = txqueue.end();
            //}
        } else {
            if (sk.ok(EPOLLIN)) {
                ;
            }
        }
        return *this;
    }

    void connect(DatagramSocket& sk, const char* ip, unsigned short port) {
        if (sk.connect(ip,port) < 0) {
            ERR_EXIT("connect");
        } else {
            //established_ = 1;
            //sa_peer_ = make_sa(ip,port);
            sk.fcntl(O_NONBLOCK, true);
            context->epoll_add(sk, EPOLLIN|EPOLLOUT);
            DEBUG("epoll.add %d", sk.fd());
        }
    }

    void bind(DatagramSocket& sk, const char* ip, unsigned short port) {
        if (sk.bind(ip,port) < 0) {
            ERR_EXIT("bind");
        } else {
            //established_ = 0;
            //sa_peer_ = make_sa(ip,port);
            sk.fcntl(O_NONBLOCK, true);
            context->epoll_add(sk, EPOLLIN|EPOLLOUT);
            DEBUG("epoll.add %d", sk.fd());
        }
    }

    DatagramIO(Context& c) :context(&c) {}

#if 0
    template <typename Sock> void do_recv(Sock& sk) {
        while (sk.ok(EPOLLIN)) {
            Recvbuffer& rb = sk.xdata();
            typename Recvbuffer::range sp = rb.spaces();
            ERR_EXIT_IF(sp.empty(),"%u %u", sp.size(), rb.size());
            int n = sk.recv(sp.begin(), sp.size());//(, sa_peer_);
            if (n < 0) {
                return error(sk, errno, __FUNCTION__);
            }
            if (n == 0) {
                BOOST_ASSERT(!sk.ok(EPOLLIN));
                break;
            }
            rb.commit(sp, n);
            process_recvd(rb);
        }
    }
    template <typename Sock> void do_send(Sock& sk) {
        //DEBUG("", sk.is_open(), sk.events() );
        while (sk.ok(EPOLLOUT) && tx_iter_ != txqueue.end()) {
            int n = sk.send(tx_iter_->begin(), tx_iter_->end());//(sa_peer_);
            if (n < 0) {
                return error(sk, errno, __FUNCTION__);
            }
            if (n == 0) {
                BOOST_ASSERT(!sk.ok(EPOLLOUT));
                DEBUG("need EPOLLOUT");
                break;
            }
            tx_iter_->consume(n);
            if (tx_iter_->empty()) {
                txqueue.done(tx_iter_);
                tx_iter_ = txqueue.end();
            }
        }
    }
#endif
};
#endif

#endif // EPOLL_HPP__

