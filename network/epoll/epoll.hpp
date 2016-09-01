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
//#include <vector>
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

        virtual void process(EPollContext* ctx, int evts) {
            events = evts;
            ctx->on_events(*this);
        }

        ~UDPMain() {
            ::close(fd);
        }
        UDPMain(EPollContext& ctx) {
            fd = ::socket(AF_INET, SOCK_DGRAM, 0);
            if (fd < 0)
                ERR_EXIT("init:socket SOCK_DGRAM");
            ctx.nonblocking(fd);

            struct sockaddr_in sa; //struct sockaddr_in clientaddr;
            bzero(&sa, sizeof(sa));
            sa.sin_family = AF_INET;
            sa.sin_addr.s_addr = (INADDR_ANY); // inet_pton;
            sa.sin_port = htons(LOCAL_PORT);
            if (::bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
                ERR_EXIT("init:bind");
            }
        }
    };

    buffer_list& buflis;
    int epfd;
    UDPMain umain;
    Thread<EPollContext> thread;
    struct txbuf {
        typename buffer_list::iterator it; // = buflis.end();
        char* pos;
    } buf;

    ~EPollContext() {
        epoll_del(umain.fd);
        ::close(epfd);
    }

    EPollContext(buffer_list& lis)
        : buflis(lis)
        , epfd(this->epoll_create())
        , umain(*this)
        , thread(*this, "EPollContext")
    {
        buf.it = buflis.end();
        epoll_add(umain.fd, EPOLLIN|EPOLLOUT, &umain);
    }

    void run() // thread func
    {
        while (!thread.stopped) {
            struct epoll_event evts[16];
            int nready = ::epoll_wait(epfd, evts, sizeof(evts)/sizeof(*evts), 500);
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
            char line[128];
            ssize_t nread;
            while ( (nread = ::read(um.fd, line, sizeof(line))) > 0) {
                ;
            }
            if (nread == 0) {
                on_closed(um);
            } else {
                if (errno == EAGAIN) {
                    um.events &= ~EPOLLIN;
                    //DEBUG("received data: %s", line);
                    //struct epoll_event ev;
                    //ev.data.fd = sockfd;
                    //ev.events = EPOLLOUT; // | EPOLLET;
                    //epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                } else {
                    on_error(um, errno);
                }
            }

        } else if(um.events & EPOLLOUT) {
            if (buf.it == buflis.end()) {
                buf.it = buflis.wait(500);
                if (buf.it == buflis.end()) {
                    DEBUG("buflis:wait %d", int(buflis.size()));
                    return;
                }
            }
            // buflis.done(buf.it);

            //write(sockfd, line, strlen(line));

            //DEBUG("written data: %s", line);

            //struct epoll_event ev;
            //ev.data.fd = sockfd;
            //ev.events = EPOLLIN; // | EPOLLET;
            //epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
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
    }
    void on_closed(UDPMain& um) {
        ERR_MSG("UDPMain %d closed", um.fd);
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

