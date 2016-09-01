#ifndef BUFFER_HPP__
#define BUFFER_HPP__

#include <stdlib.h>
#include "thread.hpp"

template <unsigned Bufsize>
struct FixBuffers
{
    struct Buffer {
        Buffer(void* p=0) : head_((char*)p), stat(0) {}
        char* begin() { return head_; }
        char* end() { return head_ + Bufsize; }
        unsigned size() const { return Bufsize; }
        char* head_;
        unsigned stat;
    };
    //enum { Bufsize = BITSTREAM_LEN };
    enum { EAlloc = 0x02, EUsable = 0x04, EUsing = 0x08 };
    enum { Bufcount = 2 };
    Buffer bufs[Bufcount];
    pthread_mutex_type mutex_;
    pthread_cond_type cond_;

    typedef Buffer* iterator;

    FixBuffers() {
        for (unsigned i=0; i<Bufcount; ++i) {
            bufs[i] = Buffer( calloc(1,Bufsize) );
        }
    }

    bool empty() const { return false; }
    unsigned size() const { return Bufcount; }
    iterator begin() { return &bufs[0]; }
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
            iterator b = &bufs[ (beg2-begin()) % size()];
            if (b->stat == xe) {
                return b;
            }
        }
        return end();
    }
};

#endif // BUFFER_HPP__

