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

inline int nonblocking(int fd)
{
    int opts = fcntl(fd, F_GETFL);
    if(opts < 0) {
        ERR_EXIT("fcntl %d F_GETFL", fd);
    }
    opts = opts | O_NONBLOCK;
    if(fcntl(fd, F_SETFL, opts) < 0) {
        ERR_EXIT("fcntl %d F_SETFL", fd);
    }
    return fd;
}
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
    virtual void forward(Context& ctx, int evts) = 0;
};

template <typename XData, int Type, typename Context>
struct EPollSocket : EventInterface<Context>, boost::noncopyable
{
    typedef EPollSocket<XData,Type,Context> ThisType;
    /// EPOLLIN | EPOLLOUT | EPOLLET

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

    ThisType& connect(const char* ip, unsigned short port) {
        struct sockaddr_in sa = make_sa(ip,port);
        open()->fcntl(O_NONBLOCK, false);
        if (::connect(fd_, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
            if (errno == EINPROGRESS) {
                DEBUG("connecting %s:%d ...", ip, (int)port);
            } else {
                ERR_EXIT("connect");
            }
        } else {
            DEBUG("connected %s:%d [OK]", ip, (int)port);
            this->fcntl(O_NONBLOCK, true);
        }
        return *this;
    }
    ThisType& bind(const char* ip, unsigned short port) {
        struct sockaddr_in sa = make_sa(ip,port);
        open()->fcntl(O_NONBLOCK, true);
        if (::bind(fd_, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
            ERR_EXIT("bind");
        }
        DEBUG("bind %s:%d", ip ? ip : "*", (int)port);
        return *this;
    }
    ThisType& listen(const char* ip, unsigned short port, int backlog=128) {
        open()->fcntl(O_NONBLOCK, true);
        int y=1;
        if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &y, sizeof(y)) < 0) {
            ERR_EXIT("setsockopt");
        }
        this->bind(ip,port);
        if (::listen(fd_, backlog) < 0) {
            ERR_EXIT("listen");
        }
        return *this;
    }
    template <typename T>
    bool accept(T& lisk) {
        struct sockaddr_in sa; //struct sockaddr_in clientaddr;
        socklen_t slen = sizeof(struct sockaddr_in);
        int sfd = ::accept(lisk.fd(), (struct sockaddr *)&sa, &slen);
        if (sfd < 0) {
            if (errno == EAGAIN)
                return false;
            ERR_EXIT("accept: %d", sfd);
        }
            char cp[64]; // = {0};
            inet_ntop(AF_INET, &sa.sin_addr, cp,64);
            DEBUG("accept: %s", cp);
        fd_ = nonblocking(sfd);
        return true;
    }

private:
    int fd_;
    uint32_t events_;
    XData xdata_; //array_buf<bufsiz> xdata_;

    virtual void forward(Context& ctx, int evts) {
        events_ = evts;
        //DEBUG("forward events %x, %d", evts, (int)ok(EPOLLOUT));
        ctx.on_events(*this);
    }

    ThisType* open() {
        if (is_open()) {
            return this;
        }
        int sfd = ::socket(AF_INET, Type, 0);
        if (sfd < 0)
            ERR_EXIT("socket:SOCK_DGRAM");
        fd_ = sfd; //nonblocking(sfd);
        return this;
    }
    int recv_(char*p, char*end, struct sockaddr_in* sa) {
        //buffer_ref::range s = recvbuf_.spaces();
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
        //recvbuf_.commit(s, n);
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
        // buf.consume(n);
        return n;
    }

    ThisType* fcntl(int msk, bool yn)
    {
        int opts = ::fcntl(fd_, F_GETFL);
        if (opts < 0) {
            ERR_EXIT("fcntl %d F_GETFL", fd_);
        }
        int prevopts = opts;
        if (yn) {
            opts |= msk; // | O_NONBLOCK;
        } else {
            opts &= ~msk;
        }
        if (opts != prevopts) {
            if(::fcntl(fd_, F_SETFL, opts) < 0) {
                ERR_EXIT("fcntl %d F_SETFL", fd_);
            }
        }
        return this;
    }
};

template <typename buffer_list>
struct NetworkIO : boost::noncopyable
{
    typedef NetworkIO<buffer_list> Context;
    typedef array_buf<1024*24-64> Recvbuffer;
    typedef EPollSocket<Recvbuffer,SOCK_DGRAM ,Context> DatagramSocket;
    typedef EPollSocket<Recvbuffer,SOCK_STREAM,Context> StreamSocket;
    typedef EPollSocket<int       ,SOCK_STREAM,Context> ListenSocket;

    Thread<NetworkIO> thread;
    buffer_list& buflis;
    int epfd;
    DatagramSocket udp_;
    StreamSocket tcp_;
    ListenSocket lisk_;

    typename buffer_list::iterator iter_; // = buflis.end();
    //std::map<uint64_t> pids_; //std::vector<struct sockaddr_in> peers_;
    struct sockaddr_in sa_peer_;

    ~NetworkIO() {
        do_close(udp_);
        ::close(epfd);
    }

    NetworkIO(buffer_list& lis, char const* connect_ip = 0, short port=9990)
        : thread(*this, "NetworkIO")
        , buflis(lis)
        , epfd(this->epoll_create())
        , udp_()
        , tcp_()
    {
        iter_ = buflis.end();
#if 0
        if (connect_ip) {
            //udp_.connect(connect_ip, port);
            sa_peer_ = make_sa(connect_ip, port);
        } else {
            udp_.bind(NULL, port);
            bzero(&sa_peer_, sizeof(sa_peer_));
        }
        epoll_add(udp_, EPOLLIN|EPOLLOUT);
#endif
        if (connect_ip) {
            tcp_.connect(connect_ip, port);
            epoll_add(tcp_, EPOLLIN|EPOLLOUT);
        } else {
            lisk_.listen(connect_ip, port);
            epoll_add(lisk_, EPOLLIN);
        }
    }

    void run() // thread func
    {
        while (!thread.stopped) {
            if (iter_ == buflis.end()) {
                iter_ = buflis.wait(10);
            }
            if (iter_ != buflis.end()) {
                do_send(tcp_); // do_recv(udp_);
            }

            struct epoll_event evts[16];
            int nready = ::epoll_wait(epfd, evts, sizeof(evts)/sizeof(*evts), 50);
            if (nready < 0) {
                ERR_EXIT("epoll_wait");
            }

            for (int i = 0; i < nready; ++i) {
                EventInterface<Context>* xif = static_cast<EventInterface<Context>*>(evts[i].data.ptr);
                xif->forward(*this, evts[i].events);
            }
        }
    }

    template <typename Sock> void on_events(Sock& sk)
    {
        if (sk.ok(EPOLLIN)) {
            return do_recv(sk);
        }
        if (sk.ok(EPOLLOUT)) {
            DEBUG("EPOLLOUT");
            return do_send(sk);
        }
    }
    template <typename Sock> void error(Sock& sk, int ec) {
        ERR_MSG("error %d", sk.fd());
        do_close(sk);
    }

    template <typename Sock> void do_close(Sock& sk) {
        if (sk.is_open()) {
            epoll_del(sk);
            sk.close();
        }
        //ERR_MSG("Socket %d closed", sk.fd());
    }

    template <typename Sock> void do_recv(Sock& sk)
    {
        while (sk.ok(EPOLLIN)) {
            Recvbuffer& buf = sk.xdata();
            Recvbuffer::range sp = buf.spaces();
            int n = sk.recv(sp.begin(), sp.end());//(, sa_peer_);
            if (n < 0) {
                return error(sk, errno);
            }
            if (n > 0) {
                fwrite(sp.begin(), n, 1, stdout);
                //buf.consume(buf.size());
            }
        }
    }
    template <typename Sock> void do_send(Sock& sk)
    {
        if (sk.type() == SOCK_DGRAM && sa_peer_.sin_port == 0) {
            return;
        }
        if (sk.type() == SOCK_STREAM) {
            //if (sk.state() == EConnecting) {
            //    int y;
            //    if (::getsockopt(lisk.fd(), SOL_SOCKET, SO_ERROR, &y, sizeof(y)) < 0) {
            //        ERR_EXIT("getsockopt");
            //    }
            //    if (y != 0) {
            //        return error(sk); //ERR_EXIT("getsockopt SO_ERROR %d", y);
            //    }
            //}
        }

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

    void do_recv(ListenSocket& lisk)
    {
        if (tcp_.is_open()) {
            DEBUG("only-one-accepted");
            return;
        }
        if (tcp_.accept(lisk))
            epoll_add(tcp_, EPOLLIN|EPOLLOUT);
    }
    void do_send(ListenSocket& lisk) {}

    // EPOLL_CTL_MOD
    template <typename T>
    void epoll_add(T& sk, int evts) {
        struct epoll_event ev;
        ev.data.ptr = static_cast<void*>(&sk);
        ev.events = evts|EPOLLET; //EPOLLIN|EPOLLOUT | EPOLLET;
        if (::epoll_ctl(epfd, EPOLL_CTL_ADD, sk.fd(), &ev) < 0)
            ERR_EXIT("init:epoll_ctl");
    }
    template <typename T>
    void epoll_del(T& sk) {
        struct epoll_event ev;
        if (epoll_ctl(epfd, EPOLL_CTL_DEL, sk.fd(), &ev) < 0)
            ERR_EXIT("deinit:epoll_ctl");
    }

    static int epoll_create() {
        int fd = ::epoll_create(16);
        if (fd < 0)
            ERR_EXIT(":epoll_create");
        return fd;
    }

};

#endif // EPOLL_HPP__

