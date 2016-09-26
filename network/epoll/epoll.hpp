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

template <typename T> struct iosel {
    static int Recv(int fd, char* buf, unsigned len, int flags, T const&) {
        return ::recvfrom(fd,buf,len, flags, NULL,0);
    }
    static int Send(int fd, char const* buf, unsigned len, int flags, T const&) {
        return ::sendto(fd,buf,len, flags, NULL,0);
    }
    static int Recv(int fd, struct iovec*iov, unsigned iovlen, int flags, T const&) {
        struct msghdr h = {0};
        h.msg_name = NULL   ; h.msg_namelen = 0;
        h.msg_control = 0   ; h.msg_controllen = 0;
        h.msg_flags = 0     ;
        h.msg_iov = iov     ; h.msg_iovlen = iovlen;
        return ::recvmsg(fd, &h, flags);
    }
    static int Send(int fd, struct iovec*iov, unsigned iovlen, int flags, T const&) {
        struct msghdr h = {0};
        h.msg_name = NULL   ; h.msg_namelen = 0;
        h.msg_control = 0   ; h.msg_controllen = 0;
        h.msg_flags = 0     ;
        h.msg_iov = iov     ; h.msg_iovlen = iovlen;
        return ::sendmsg(fd, &h, flags);
    }
};
template <> struct iosel<struct sockaddr_in> {
    static int Recv(int fd, char* buf, unsigned len, int flags, struct sockaddr_in& sa) {
        socklen_t salen = sizeof(struct sockaddr_in);
        return ::recvfrom(fd,buf,len, flags, (struct sockaddr*)&sa, &salen);
    }
    static int Send(int fd, char const* buf, unsigned len, int flags, struct sockaddr_in&sa) {
        return ::sendto(fd,buf,len, flags, (struct sockaddr*)&sa, sizeof(struct sockaddr_in));
    }
    static int Recv(int fd, struct iovec*iov, unsigned iovlen, int flags, struct sockaddr_in& sa) {
        struct msghdr h = {0};
        h.msg_name = (void*)&sa ; h.msg_namelen = sizeof(struct sockaddr_in);
        h.msg_control = 0       ; h.msg_controllen = 0;
        h.msg_flags = 0         ;
        h.msg_iov = iov         ; h.msg_iovlen = iovlen;
        return ::recvmsg(fd, &h, flags);
    }
    static int Send(int fd, struct iovec*iov, unsigned iovlen, int flags, struct sockaddr_in& sa) {
        struct msghdr h = {0};
        h.msg_name = (void*)&sa ; h.msg_namelen = sizeof(struct sockaddr_in);
        h.msg_control = 0       ; h.msg_controllen = 0;
        h.msg_flags = 0         ;
        h.msg_iov = iov         ; h.msg_iovlen = iovlen;
        return ::sendmsg(fd, &h, flags);
    }
};

struct EmptyStruct {};

template <int Type, typename Context, typename XBuffer, typename XData=EmptyStruct, typename L2XData=EmptyStruct>
struct EPollSocket : EventInterface<Context>, boost::noncopyable
{
    typedef EPollSocket<Type,Context,XBuffer,XData,L2XData> ThisType;
    typedef XBuffer xbuffer_type;
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
    //EPollSocket(std::ios::openmode) {
    //    fd_=open();
    //    events_=0;
    //}

    XBuffer& xbuf() { return xbuf_; }
    XData&   xdata() { return xdata_; }
    L2XData& l2xdata() { return l2xdata_; }
    template <typename T> void l2xdata(T&& x) { l2xdata_ = std::forward<T>(x); }

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

    int recv(char      *p, unsigned len) { return recv_ret_( iosel<L2XData>::Recv(fd_,p,len,0, l2xdata_), len); }
    int send(char const*p, unsigned len) { return send_ret_( iosel<L2XData>::Send(fd_,p,len,0, l2xdata_), len); }
    int recv(struct iovec*iov, unsigned iovlen) { return recv_ret_( iosel<L2XData>::Recv(fd_,iov,iovlen,0, l2xdata_), iovlen_sum(iov,iovlen)); }
    int send(struct iovec*iov, unsigned iovlen) { return send_ret_( iosel<L2XData>::Send(fd_,iov,iovlen,0, l2xdata_), iovlen_sum(iov,iovlen)); }

    //int recv(char*p, unsigned len, struct sockaddr_in& sa) { return recv_(p,len, &sa,sizeof(sa)); }
    //int recv(struct iovec*iov, unsigned iovlen, struct sockaddr_in& sa) { return recv_(iov,iovlen, &sa); }
    //int send(char const*p, unsigned len, struct sockaddr_in& sa) { return send_(p,len, &sa,sizeof(sa)); }
    //int send(struct iovec*iov, unsigned iovlen, struct sockaddr_in& sa) { return send_(iov,iovlen, &sa); }

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

    /// moveable
    EPollSocket(EPollSocket&& rhs)
        : xbuf_(std::move(rhs.xbuf_))
        , xdata_(std::move(rhs.xdata_))
        , l2xdata_(std::move(rhs.l2xdata_))
    {
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
            xbuf_ = std::move(rhs.xbuf_);
            xdata_ = std::move(rhs.xdata_);
            l2xdata_ = std::move(rhs.l2xdata_);
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
    int fd_;
    uint32_t events_;
    //struct XDataWrapper : XData {
    //    struct FrameworkDataWrap : L2XData {
    //    } fwdata;
    //} xdata_;
    XBuffer xbuf_;
    XData xdata_;
    L2XData l2xdata_;
    // XData xdata_; //array_buf<bufsiz> xdata_;

    // sendmsg; recvmsg; // struct iovec *iov
    //int recv_(struct iovec*iov, unsigned iovlen, struct sockaddr_in* sa) {
    //    struct msghdr h = {0};
    //    h.msg_name = (void*)sa;
    //    h.msg_namelen = sizeof(struct sockaddr_in);
    //    h.msg_control = 0;
    //    h.msg_controllen = 0;
    //    h.msg_flags = 0;
    //    h.msg_iov = iov;
    //    h.msg_iovlen = iovlen;
    //    return recv_ret_( ::recvmsg(fd_, &h, 0), iovlen_sum(iov,iovlen) );
    //}
    //int send_(struct iovec*iov, unsigned iovlen, struct sockaddr_in* sa) {
    //    struct msghdr h = {0};
    //    h.msg_name = (void*)sa;
    //    h.msg_namelen = sizeof(struct sockaddr_in);
    //    h.msg_control = 0;
    //    h.msg_controllen = 0;
    //    h.msg_flags = 0;
    //    h.msg_iov = iov;
    //    h.msg_iovlen = iovlen;
    //    return send_ret_( ::sendmsg(fd_, &h, 0), iovlen_sum(iov,iovlen) );
    //}
    //int recv_(char*p, unsigned xlen, struct sockaddr_in* sa, socklen_t slen) {
    //    return recv_ret_( ::recvfrom(fd_, p, xlen, 0, (struct sockaddr*)sa,slen), xlen );
    //}
    //int send_(char const* p, unsigned xlen, struct sockaddr_in* sa, socklen_t slen) {
    //    return send_ret_( ::sendto(fd_, p, xlen, 0, (struct sockaddr*)sa, slen), xlen );
    //}

    int recv_ret_(int ret, int xlen) {
        if (ret < 0 || (Type==SOCK_STREAM && ret < xlen)) {
            events_ &= ~EPOLLIN;
        }
        if (ret < 0) {
            if (errno == EAGAIN) {
                return 0;
            }
            LOGE("recvfrom: %s", strerror(errno));
            return ret;
        }
        if (Type==SOCK_STREAM && ret == 0) {
#if defined(__arm__) && (__GNUC__ == 4) && (__GNUC_MINOR__ == 4)
#else
            errno = EPIPE;
#endif
            LOGE("recvfrom:CLOSE:EPIPE");
            return -1; //int(errno = EPIPE);
        }
        return ret;
    }
    int send_ret_(int ret, int xlen) {
        if (ret < xlen)
            events_ &= ~EPOLLOUT;
        if (ret < 0) {
            if (errno == EAGAIN)
                return 0;
            LOGE("sendto: %s", strerror(errno));
            //return ret;
        }
        return ret;
    }

    int iovlen_sum(struct iovec*iov, unsigned iovlen) {
        int sum = 0;
        for (unsigned i=0; i < iovlen; ++i)
            sum += iov[i].iov_len;
        return sum;
    }

private:
    EPollSocket(EPollSocket&);
    EPollSocket& operator=(EPollSocket&);
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

//template <typename Context, typename ListenSocket> struct ListenIO {
//    Context* context;
//    void process_io(ListenSocket& lisk) {
//        typedef typename ListenSocket::XDataType StreamSocket;
//        StreamSocket& tmpsk = lisk.xbuf();
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
//        context->epoll.add(lisk, EPOLLIN);
//    }
//};

template <typename Context, typename StreamSocket>
struct StreamIO
{
    void process_io(StreamSocket& sk) {
        if (sk.ok(EPOLLIN)) {
            context->l1recv(sk);
        }
        if (sk.ok(EPOLLOUT)) {
            context->l1send(sk);
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
    template <typename Context, typename XBuffer>
    struct traits
    {
        typedef EPollSocket<SOCK_STREAM,Context, XBuffer> streamsk_type;
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
                streamsk_type& sk = lisk.xbuf();
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

            void l2close(streamsk_type& sk) {
                sk.xbuf() = XBuffer();
                this->context->l1close(sk);
            }
            void l2close(lisk_type& lisk) {
                streamsk_type& sk = lisk.xbuf();
                sk.xbuf() = XBuffer();
                this->context->l1close(sk);
                //sk = streamsk_type();
                this->context->l1close(lisk);
            }

            void prewait(lisk_type& lisk, int) {
                streamsk_type& sk = lisk.xbuf();
                if (sk.is_open()) {
                    this->context->trysendx(sk);
                }
            }
        };
    };
};

struct TCPClient {
    template <typename Context, typename XBuffer>
    struct traits
    {
        typedef EPollSocket<SOCK_STREAM, Context, XBuffer> socket;
        struct io : StreamIO<Context,socket>
        {
            io(Context& c, socket& sk, char const*ip, int port)
                : StreamIO<Context,socket>(c)
            {
                sk.open();
                sk.connect(ip, port);
                this->setup(sk);
            }
            void l2close(socket& sk) {
                sk.xbuf() = XBuffer();
                this->context->l1close(sk);
            }

            void process_io(socket& sk) {
                StreamIO<Context,socket>::process_io(sk);
            }

            void prewait(socket& sk, int) {
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
    typedef typename std::decay<TXQueue>::type txqueue_type;

    EPoll epoll;
    TXQueue txqueue;
    RXSink rxsink;

    typename txqueue_type::iterator txq_iter_; // = txqueue.end();
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
        socket_io.l2close(socket);
    }
    NetworkIO(TXQueue& txq, RXSink& rxs, char const* ip, int port)
        : txqueue(txq) , rxsink(rxs)
        , socket_io(*this, socket, (ip?ip:"*"), port)
    {
        DEBUG("");
        tx_p_ = 0; //txq_iter_ = txqueue.end();
    }

    void xpoll(bool* stopped) // thread func
    {
        DEBUG("");
        while (!*stopped) {
            socket_io.prewait(socket, 0);

            struct epoll_event evts[16];
            int nready = epoll.wait(evts, sizeof(evts)/sizeof(*evts), 5);
            if (nready < 0) {
                if (errno == EINTR)
                    continue;
                ERR_EXIT("epoll_wait: %s", strerror(errno));
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
            if ( (txq_iter_ = txqueue.wait(0)) != txqueue.end()) {
                tx_p_ = txq_iter_->begin();
            }
        }
        if (tx_p_) {
            l1send(sk); // l1recv(dgramsk_);
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
        LOGE("%s: %d: %s", ps, sk.fd(), strerror(ec));
        socket_io.l2close(sk);

        if (tx_p_) {
            tx_p_ = 0;
            txqueue.done(txq_iter_);
        }
    }

    template <typename Sock> void l1close(Sock& sk) {
        if (sk.is_open()) {
            LOGE("epoll.del %d", sk.fd());
            epoll.del(sk);
            sk.close();
        }
    }
    template <typename Sock> void l1recv(Sock& sk) {
        BOOST_ASSERT(sk.ok(EPOLLIN));
        while (sk.ok(EPOLLIN)) {
            Recvbuffer& rb = sk.xbuf();
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
    template <typename Sock> void l1send(Sock& sk) {
        //DEBUG("", sk.is_open(), sk.events() );
        while (sk.ok(EPOLLOUT) && tx_p_) {
            int xlen = txq_iter_->end() - tx_p_;
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
                txqueue.done(txq_iter_);
                tx_p_ = 0; // txq_iter_ = txqueue.end();
            } else {
                tx_p_ += n; // txq_iter_->consume(n);
            }
        }
    }

    template <typename Sock> void l1recv(Sock& sk, struct sockaddr_in& sa) {
        while (sk.ok(EPOLLIN)) {
            Recvbuffer& rb = sk.xbuf();
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
    template <typename Sock> void l1send(Sock& sk, struct sockaddr_in& sa) {
        ;
    }

    // bridge //std::tuple<>
    template <typename Sock> void process_io(Sock& sk) { socket_io.process_io(sk); }
};

namespace udp {
enum { MTU = 1456 };

struct client;
struct server;

template <typename T, typename Context, typename socket>
struct io_basic;

template <typename Context, typename socket>
struct io_basic<void,Context,socket>
{
    Context* context;
    typename Context::txqueue_type* txque_;
    typename Context::txqueue_type::iterator txiter_;
    char* xpos_;
    union { uint32_t u32; uint16_t u16[2]; } xh_;

    time_t recvtime_;

    void l2close(socket& sk) {
        BOOST_ASSERT(sk.is_open());
        // context->l1close(sk);
        this->context->epoll.del(sk);
        if (this->txiter_ != this->txque_->end()) {
            this->txque_->done(this->txiter_);
            this->txiter_ = this->txque_->end();
        }
        sk.xbuf() = typename socket::xbuffer_type();
        //sk.l2xdata() = EmptyStruct();
        sk.close();
    }

    void l2send(socket& sk)
    {
        BOOST_ASSERT(sk.ok(EPOLLOUT));

        boost::iterator_range<char*> const& data = *txiter_;
        BOOST_ASSERT(!data.empty() && xpos_>=data.begin() && xpos_<data.end());

        unsigned xlen = data.end() - xpos_;
        struct iovec iov[2];
        iov[1].iov_base = xpos_;
        iov[1].iov_len = std::min( unsigned(MTU - sizeof(xh_)), xlen );

        uint16_t head[2] = { xh_.u16[0], xh_.u16[1]++ };
        if (iov[1].iov_len == xlen) {
            head[1] |= 0x8000;
        }
        head[0] = htons(head[0]); head[1] = htons(head[1]);
        iov[0].iov_base = (void*)&head;
        iov[0].iov_len = sizeof(head);

        int slen = sk.send(iov, 2);
        if (slen < 0) {
            LOGE("%s", strerror(errno));
            l2close(sk);
            return;
        }
        if (slen < int(sizeof(head)+xlen)) {
            LOGE("size %u", sizeof(head)+xlen);
            l2close(sk);
            return;
        }
        xpos_ += xlen; //slen - sizeof(head);
        if (xpos_ == data.end()) {
            xpos_ = 0;
            this->txque_->done(this->txiter_);
            this->txiter_ = this->txque_->end();
        }
    }

    void l2recv(socket& sk)
    {
        typename socket::xbuffer_type& xbuf = sk.xbuf();
        typename socket::xbuffer_type::range sp = xbuf.spaces();
        uint16_t head[2] = { 0, 0 };

        struct iovec iov[2];
        iov[0].iov_base = (void*)&head;
        iov[0].iov_len = sizeof(head);
        iov[1].iov_base = sp.begin();
        iov[1].iov_len = sp.size();

        int rlen = sk.recv(iov, 2);
        if (rlen < 0) {
            LOGE("%s", strerror(errno));
            l2close(sk);
            return;
        }
        if (rlen == 0) {
            LOGV("recvd==0");
            return;
        }
        if (rlen < (int)sizeof(head)) {
            LOGE("rlen %u", rlen);
            l2close(sk);
            return;
        }
        if (head[1] == 0xffff) {
            if (rlen > 8) {
                LOGE("%d",rlen);
                l2close(sk);
            }
            return;
        }
        head[0] = ntohs(head[0]); head[1] = ntohs(head[1]);

        if ((head[1] & 0x7fff) == xh_.u16[1]) {
            xh_.u16[1]++;
            xbuf.commit(sp, rlen - sizeof(head));
            if (head[1] & 0x8000) {
                xh_.u16[1] = 0; //xh_.u16[0] ++; //= head[0];
                context->process_recvd(xbuf); //TODO
            }
        } else /*if (xh_.u16[1] != 0xffff)*/ {
            LOGE("*NOT* expeted %x %#04x, %x", xh_.u16[1], head[1], head[0]);
            if (!xbuf.empty())
                xbuf.consume(xbuf.size());
            xh_.u16[1] = 0; //0xffff;
        }
    }

    //void process_recvd(XBuffer& xbuf) {
    //    char* p = xbuf.begin();
    //    while (p < xbuf.end()) {
    //        int n = context->process_recvd(xbuf);
    //        if (n < 0) {
    //            LOGE("process_recvd: %d", n);
    //            l2close();
    //            return;
    //        } else if (n == 0)
    //            break;
    //        p += n;
    //    }
    //    if (p > xbuf.begin())
    //        xbuf.consume(p - xbuf.begin());
    //    //return xbuf.size();
    //}

    void process_io(socket& sk) {
        if (sk.ok(EPOLLIN)) {
            recvtime_ = time(0);
            l2recv(sk);
        }
        if (sk.ok(EPOLLOUT)) {
            if (this->txiter_ != this->txque_->end()) {
                l2send(sk);
            }
        }
    }

    void prewait(socket& sk, int millis) {
        if (!has_peer(sk))
            return;
        bool sent = 0;
        while (sk.ok(EPOLLOUT)) {
            if (this->txiter_ == this->txque_->end()) {
                this->txiter_ = this->txque_->wait(millis);
                if (this->txiter_ == this->txque_->end()) {
                    break;
                } else {
                    xpos_ = this->txiter_->begin();
                    xh_.u16[0]++;
                    xh_.u16[1] = 0;
                }
            } else {
                l2send(sk);
                sent = 1;
            }
        }
        if (!sent) {
            time_t t = time(0);
            if (t - recvtime_ > 3) {
                uint32_t u= 0xffffffff;
                sk.send((char*)&u,sizeof(u));
                recvtime_ = t;
            }
        }
    }

    bool has_peer(socket& sk) const { return bool(sk.l2xdata().sin_port); }

    io_basic(Context& c) {
        this->context = &c;
        this->recvtime_ = 0; // sk.send("",0);

        this->xh_.u32 = 0;
        this->xpos_ = 0;
        this->txque_ = &this->context->txqueue;
        this->txiter_ = this->txque_->end();
    }
};

template <typename Context, typename socket>
struct io_basic<client,Context,socket> : io_basic<void,Context,socket>
{
    io_basic(Context& c, socket& sk, char const*ip, int port)
        : io_basic<void,Context,socket>(c)
    {
        sk.open();
        sk.l2xdata() = sk.make_sa(ip,port); //sk.connect(ip, port);
        sk.nonblocking();
        this->context->epoll.add(sk, EPOLLOUT|EPOLLIN);
        DEBUG("epoll.add %d %s:%d udp:client", sk.fd(), ip, port);
    }
};

template <typename Context, typename socket>
struct io_basic<server,Context,socket> : io_basic<void,Context,socket>
{
    io_basic(Context& c, socket& sk, char const*ip, int port)
        : io_basic<void,Context,socket>(c)
    {
        sk.open();
        sk.bind(ip, port);
        sk.nonblocking();
        this->context->epoll.add(sk, EPOLLOUT|EPOLLIN);
        DEBUG("epoll.add %d %s:%d udp:server", sk.fd(), ip, port);
    }
};

struct client {
    template <typename Context, typename XBuffer>
    struct traits
    {
        typedef EPollSocket<SOCK_DGRAM, Context, XBuffer, EmptyStruct, struct sockaddr_in> socket;
        typedef io_basic<client,Context,socket> io;
    };
};
struct server {
    template <typename Context, typename XBuffer>
    struct traits
    {
        typedef EPollSocket<SOCK_DGRAM, Context, XBuffer, EmptyStruct, struct sockaddr_in> socket;
        typedef io_basic<server,Context,socket> io;
    };
};

} // namespace udp

#endif // EPOLL_HPP__

