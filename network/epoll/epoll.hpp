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

template <typename buffer_list>
struct EPollContext
{
    typedef EPollContext<buffer_list> Context;

    struct EventInterface {
        virtual ~EventInterface() {}
        virtual void process(EPollContext* ctx, int evts) = 0;
    };

    struct UDPMain : EventInterface {
        enum { LOCAL_PORT=5555 };
        int fd;
        uint32_t events;

        array_buf<1024*24> recvbuf;

        virtual void process(EPollContext* ctx, int evts) {
            events = evts;
            ctx->on_events(*this);
        }

        ~UDPMain() {
            if (fd >= 0)
                ::close(fd);
        }
        UDPMain(EPollContext& ctx) {
            fd = ::socket(AF_INET, SOCK_DGRAM, 0);
            if (fd < 0)
                ERR_EXIT("init:socket SOCK_DGRAM");
            ctx.nonblocking(fd);
        }

        void connect(const char* ip, unsigned short port) {
            struct sockaddr_in sa;
            bzero(&sa, sizeof(sa));
            sa.sin_family = AF_INET;
            if (inet_pton(AF_INET, ip, &sa.sin_addr) < 0) {
                ERR_EXIT("UDPMain:inet_pton");
            }
            sa.sin_port = htons(port);
            if (::connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
                ERR_EXIT("connect");
            }
            DEBUG("connect %s:%d", ip, (int)port);
        }
        void listen(const char* ip, unsigned short port) {
            struct sockaddr_in sa;
            bzero(&sa, sizeof(sa));
            sa.sin_family = AF_INET;
            if (ip) {
                if (inet_pton(AF_INET, ip, &sa.sin_addr) < 0) {
                    ERR_EXIT("UDPMain:inet_pton");
                }
            } else {
                sa.sin_addr.s_addr = INADDR_ANY;
            }
            sa.sin_port = htons(port);
            if (::bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
                ERR_EXIT("listen:bind");
            }
            DEBUG("listen %s:%d", ip ? ip : "*", (int)port);
        }

        int recv_(struct sockaddr_in* sa) {
            if (recvbuf.empty(0) || !(events & EPOLLIN)) {
                ERR_EXIT("recv error: %u %d", recvbuf.size(0), int(events & EPOLLIN));
            }
            socklen_t slen = sizeof(struct sockaddr_in);
            int n = ::recvfrom(fd, recvbuf.begin(0), recvbuf.size(0), 0, (struct sockaddr*)sa, sa?&slen:NULL);
            if (n < (int)recvbuf.size(0))
                events &= ~EPOLLIN;
            if (n < 0) {
                if (errno == EAGAIN)
                    return 0;
                ERR_MSG("recv");
                return n;
            }
            if (n == 0) {
                return -(errno = EPIPE);
            }
            recvbuf.commit(n);
            return n;
        }
        int recv(struct sockaddr_in& sa) {
            return recv_(&sa);
        }
        int recv() {
            return recv_(NULL);
        }

        int send_(buffer_ref& buf, struct sockaddr_in* sa, socklen_t slen) {
            if (buf.empty() || !(events & EPOLLOUT)) {
                ERR_EXIT("send error: %u %d", buf.size(), int(events & EPOLLOUT));
            }
            int n = ::sendto(fd, buf.begin(), buf.size(), 0, (struct sockaddr*)sa, slen);
            if (n < (int)buf.size())
                events &= ~EPOLLOUT;
            if (n < 0) {
                if (errno == EAGAIN)
                    return 0;
                ERR_MSG("send");
                return n;
            }
            buf.consume(n);
            return n;
        }
        int send(buffer_ref& buf, struct sockaddr_in& sa) {
            return send_(buf, &sa, sizeof(sa));
        }
        int send(buffer_ref& buf) {
            return send_(buf, NULL, 0);
        }
        //int send(char* p, char* end) {
        //    int nbytes = int(end - p);
        //    if (p <= end || !(events & EPOLLOUT)) {
        //        ERR_EXIT("send error: %d %d", nbytes, int(events & EPOLLOUT));
        //    }
        //    int n = ::send(fd, p, nbytes, 0);
        //    if (n < nbytes)
        //        events &= ~EPOLLOUT;
        //    if (n < 0) {
        //        if (errno == EAGAIN)
        //            return 0;
        //        ERR_MSG("send");
        //    }
        //    return n;
        //}
        void close() {
            if (fd >= 0) {
                ::close(fd);
                fd = -1;
            }
        }
    };

    buffer_list& buflis;
    int epfd;
    UDPMain umain;
    Thread<EPollContext> thread;

    typename buffer_list::iterator iter_; // = buflis.end();
    //std::map<uint64_t> pids_; //std::vector<struct sockaddr_in> peers_;
    struct sockaddr_in sa_;

    ~EPollContext() {
        do_close(umain);
        ::close(epfd);
    }

    EPollContext(buffer_list& lis, char const* connect_ip = 0, short port=9990)
        : buflis(lis)
        , epfd(this->epoll_create())
        , umain(*this)
        , thread(*this, "EPollContext")
    {
        if (connect_ip) {
            // umain.connect(connect_ip, port);
            bzero(&sa_, sizeof(sa_));
            sa_.sin_family = AF_INET;
            inet_pton(AF_INET, connect_ip, &sa_.sin_addr);
            sa_.sin_port = htons(port);
        } else {
            umain.listen(NULL, port);
            bzero(&sa_, sizeof(sa_));
        }
        iter_ = buflis.end();
        epoll_add(umain.fd, EPOLLIN|EPOLLOUT, &umain);
    }

    void run() // thread func
    {
        while (!thread.stopped) {
            if (iter_ == buflis.end()) {
                iter_ = buflis.wait(10);
            }
            if (iter_ != buflis.end()) {
                do_send(umain); // do_recv(umain);
            }

            struct epoll_event evts[16];
            int nready = ::epoll_wait(epfd, evts, sizeof(evts)/sizeof(*evts), 50);
            if (nready < 0) {
                ERR_EXIT("epoll_wait");
            }

            for (int i = 0; i < nready; ++i) {
                EventInterface* xif = static_cast<EventInterface*>(evts[i].data.ptr);
                xif->process(this, evts[i].events);
            }
        }
    }

    void on_events(UDPMain& um)
    {
        if (um.events & EPOLLIN) {
            return do_recv(um);
        }
        if (um.events & EPOLLOUT) {
            return do_send(um);
        }
        //if (events[i].data.ptr == 0) { // listen socket
        //    DEBUG("accept connection, fd is %d", mysock);
        //    struct sockaddr_in sa; //struct sockaddr_in clientaddr;
        //    socklen_t slen;
        //    int sfd = accept(mysock, (struct sockaddr *)&sa, &slen);
        //    if (sfd < 0) {
        //        ERR_EXIT("sfd < 0");
        //    }
        //    nonblocking(sfd);
        //    DEBUG("connect from %s", inet_ntoa(sa.sin_addr));
        //    struct epoll_event ev;
        //    ev.data.fd = sfd;
        //    ev.events = EPOLLIN; // | EPOLLET;
        //    epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &ev);
        //}
    }
    void on_error(UDPMain& um, int ec) {
        ERR_EXIT("UDPMain %d error", um.fd);
        do_close(um);
    }

    void do_close(UDPMain& um) {
        if (um.fd >= 0) {
            epoll_del(um.fd);
            um.close();
        }
        //ERR_MSG("UDPMain %d closed", um.fd);
    }

    void do_recv(UDPMain& um)
    {
        // struct sockaddr_in sa;
        int n;
        while ((um.events & EPOLLIN) && (n = um.recv(sa_)) > 0) {
            char* p = um.recvbuf.begin();
            fwrite(p, um.recvbuf.size(), 1, stdout);
            um.recvbuf.consume(um.recvbuf.size());
        }

        if (n < 0) {
            return on_error(um, errno);
        }
        //DEBUG("received data: %s", line);
        //struct epoll_event ev;
        //ev.data.fd = sockfd;
        //ev.events = EPOLLOUT; // | EPOLLET;
        //epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
    }
    void do_send(UDPMain& um)
    {
        if (sa_.sin_port == 0) {
            return;
        }
        while ((um.events & EPOLLOUT) && iter_ != buflis.end()) {
            int n = um.send(*iter_, sa_); //(iter_->begin(), iter_->end());
            if (n < 0) {
                return on_error(um, errno);
            }
            if (iter_->empty()) {
                buflis.done(iter_);
                iter_ = buflis.end();
            }
        }
            //struct epoll_event ev;
            //ev.data.fd = sockfd;
            //ev.events = EPOLLIN; // | EPOLLET;
            //epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
    }

    template <typename T>
    void epoll_add(int fd, int evts, T* xptr) {
        struct epoll_event ev;
        ev.data.ptr = static_cast<void*>(xptr);
        ev.events = evts|EPOLLET; //EPOLLIN|EPOLLOUT | EPOLLET;
        if (::epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0)
            ERR_EXIT("init:epoll_ctl");
    }
    void epoll_del(int fd) {
        struct epoll_event ev;
        if (epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &ev) < 0)
            ERR_EXIT("deinit:epoll_ctl");
    }

    static int epoll_create() {
        int fd = ::epoll_create(16);
        if (fd  < 0)
            ERR_EXIT(":epoll_create");
        return fd;
    }

    static void nonblocking(int fd) {
        int opts = fcntl(fd, F_GETFL);
        if(opts < 0) {
            ERR_EXIT("fcntl %d F_GETFL", fd);
        }
        opts = opts | O_NONBLOCK;
        if(fcntl(fd, F_SETFL, opts) < 0) {
            ERR_EXIT("fcntl %d F_SETFL", fd);
        }
    }
};

#endif // EPOLL_HPP__

