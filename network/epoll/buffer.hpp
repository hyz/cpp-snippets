#ifndef BUFFER_HPP__
#define BUFFER_HPP__

#include <stdlib.h>
#include <algorithm>
#include "log.hpp"
#include "thread.hpp"

struct buffer_ref
{
    unsigned capacity() const { return end_ - begin_; }

    char* begin() const { return begin_; }
    char* end() const { return cur_; }
    unsigned size() const { return end() - begin(); }
    bool empty() const { return end()==begin(); }
    char* begin(int) const { return cur_; }
    char* end(int) const { return end_; }
    unsigned size(int) const { return end(1) - cur_; }
    bool empty(int) const { return end(1)==begin(1); }

    void consume(unsigned len) {
        if (len > size()) {
            ERR_EXIT("recvbuf:consume %u", len);
        }
        if (len > 0) {
            memcpy(begin(), begin()+len, size() - len);
            cur_ -= len;
        }
    }
    void commit(unsigned len) {
        if (len > size(0)) {
            ERR_EXIT("recvbuf:commit %u", len);
        }
        cur_ += len;
    }
    void put(char* p, char* end) {
        unsigned n = std::min(this->size(0), unsigned(end - p));
        if (n < unsigned(end - p)) {
            ERR_EXIT("buffer_ref:put overflow %u", n);
        }
        memcpy(begin(0), p, n);
        commit(n);
    }

    buffer_ref(char* b, char* c, char* e) {
        begin_ = b;
        cur_ = c;
        end_ = e;
    }
    buffer_ref() { begin_ = cur_ = end_ = 0; }

    char *begin_, *cur_, *end_;
};

template <unsigned Capacity>
struct array_buf : buffer_ref
{
    array_buf() : buffer_ref(&arr_[0], &arr_[0], &arr_[Capacity])
    {}
    char arr_[Capacity];
};

template <unsigned BufSiz>
struct buffer_list_fix
{
    struct statbuf : buffer_ref {
        statbuf(char*b, char*c, char*e) : buffer_ref(b,c,e) { stat=0; }
        statbuf() { stat=0; }
        unsigned stat;
    };
    enum { EAlloc = 0x02, EUsable = 0x04, EUsing = 0x08 };

    enum { Bufcount = 2 };
    statbuf bufs_[Bufcount];
    pthread_mutex_type mutex_;
    pthread_cond_type cond_;

    typedef statbuf* iterator;

    ~buffer_list_fix() {
        for (unsigned i=0; i<Bufcount; ++i) {
            free(bufs_[i].begin());
        }
    }
    buffer_list_fix() {
        for (unsigned i=0; i<Bufcount; ++i) {
            char* p = (char*)malloc(BufSiz);
            bufs_[i] = statbuf(p, p, p+BufSiz);
        }
    }

    bool empty() const { return false; }
    unsigned size() const { return Bufcount; }
    iterator begin() { return &bufs_[0]; }
    iterator end() { return begin()+size(); }
    iterator last() { return end()-1; }

    iterator alloc(iterator prev_hint) {
        pthread_mutex_lock_guard lk(mutex_);
        iterator b = find_(0, prev_hint);
        if (!b) {
            b = find_(EUsable, prev_hint);
            if (!b)
                ERR_EXIT("buf:alloc");
        }
        b->stat = EAlloc;
        return b;
    }

    iterator wait(unsigned millis) {
        pthread_mutex_lock_guard lk(mutex_);
        iterator b = find_(EUsable, begin());
        if (!b) {
            cond_.wait(mutex_, millis);
            b = find_(EUsable, begin());
            if (!b)
                return end();
        }
        b->stat = EUsing;
        return b;
    }

    void done(iterator b) {
        if (b->stat == EAlloc) {
            b->stat = EUsable;
            cond_.signal();
        } else if (b->stat == EUsing) {
            b->stat = 0;
        } else {
            ERR_EXIT("buf:stat");
        }
    }

    iterator find_(unsigned xe, iterator beg2) {
        ++beg2;
        iterator end2 = beg2+size();
        for (; beg2 < end2; ++beg2) {
            iterator b = &bufs_[ (beg2-begin()) % size()];
            if (b->stat == xe) {
                return b;
            }
        }
        return end();
    }
};

#endif // BUFFER_HPP__

