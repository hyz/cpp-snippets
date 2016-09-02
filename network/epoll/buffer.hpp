#ifndef BUFFER_HPP__
#define BUFFER_HPP__

#include <stdlib.h>
#include <algorithm>
#include <boost/range.hpp>
#include "log.hpp"
#include "thread.hpp"

struct buffer_ref : boost::iterator_range<char*>
{
    typedef boost::iterator_range<char*> range;

    unsigned capacity() const { return size_fr(begin()); }

    range spaces() const { return boost::iterator_range<char*>(end(), end_); }

    void commit(range& sub, unsigned len) {
        if (sub.begin() != end() || len > size_fr(end())) {
            ERR_EXIT("buffer_ref:commit %u", len);
        }
        advance_end(len); // cur_ += len;
    }
    void put(char const* p, unsigned len) {
        //unsigned len = end-p; //std::min(size_fr(this->end()), unsigned(end - p));
        if (len > size_fr(end())) {
            ERR_EXIT("buffer_ref:put %u > %u", len, size_fr(end()));
        }
        memcpy(end(), p, len);
        advance_end(len); // cur_ += len;
    }
    void consume(unsigned len) {
        if (len > size()) {
            ERR_EXIT("buffer_ref:consume %u", len);
        }
        if (len > 0 && len < size()) {
            memmove(begin(), begin()+len, size() - len);
        }
        advance_end(-int(len)); // cur_ -= len;
    }

    buffer_ref(char* b, char* e, char* e2)
        : boost::iterator_range<char*>(b,e), end_(e2)
    {}
    buffer_ref(char* nil=0) : boost::iterator_range<char*>(nil,nil), end_(nil) {}

    //char* begin() const { return begin_; }
    //char* end() const { return cur_; }
    //unsigned size() const { return end() - begin(); }
    //bool empty() const { return end()==begin(); }

    //char* begin(int) const { return cur_; }
    //char* end(int) const { return end_; }
    //unsigned size(int) const { return end(1) - cur_; }
    //bool empty(int) const { return end(1)==begin(1); }

    inline unsigned size_fr(char* p) const { return unsigned(end_ - p); }
    char * end_; //*begin_, *cur_, *end_;
};

template <unsigned Capacity>
struct array_buf : buffer_ref
{
    array_buf() : buffer_ref(&array_[0], &array_[0], &array_[Capacity])
    {}
    char array_[Capacity];
};

template <unsigned BufSiz>
struct buffer_list_fix
{
    struct statful_buf : buffer_ref {
        statful_buf(char*b, char*c, char*e) : buffer_ref(b,c,e) { stat=0; }
        statful_buf() { stat=0; }
        unsigned stat;
    };
    enum { EAlloc = 0x02, EUsable = 0x04, EUsing = 0x08 };

    enum { Bufcount = 2 };
    statful_buf bufs_[Bufcount];
    pthread_mutex_type mutex_;
    pthread_cond_type cond_;

    typedef statful_buf* iterator;

    ~buffer_list_fix() {
        for (unsigned i=0; i<Bufcount; ++i) {
            free(bufs_[i].begin());
        }
    }
    buffer_list_fix() {
        for (unsigned i=0; i<Bufcount; ++i) {
            char* p = (char*)malloc(BufSiz);
            bufs_[i] = statful_buf(p, p, p+BufSiz);
        }
    }

    bool empty() const { return false; }
    unsigned size() const { return Bufcount; }
    iterator begin() { return &bufs_[0]; }
    iterator end() { return begin()+size(); }
    iterator last() { return end()-1; }

    iterator alloc(iterator prev_hint) {
        //validate_it(prev_hint);
        pthread_mutex_lock_guard lk(mutex_);
        iterator b = find_(0, prev_hint);
        if (b == end()) {
            b = find_(EUsable, prev_hint);
            if (b == end())
                ERR_EXIT("not-found");
        }
        //DEBUG("alloc:buf: %d %x, %u %u, %p %p %p", int(b-begin()), b->stat, b->size(), b->capacity(), b->begin(), b->end(), b->end_);
        b->consume(b->size());
        b->stat = EAlloc;
        return b;
    }

    iterator wait(unsigned millis) {
        pthread_mutex_lock_guard lk(mutex_);
        iterator b = find_(EUsable, begin());
        if (b == end()) {
            cond_.wait(mutex_, millis);
            b = find_(EUsable, begin());
            if (b == end())
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

    iterator find_(unsigned xe, iterator b2) {
        ++b2;
        iterator end2 = b2+size();
        for (; b2 < end2; ++b2) {
            iterator b = &bufs_[ (b2-begin()) % size()];
            if (b->stat == xe) {
                //DEBUG("find: %d %x", int(b-begin()),xe);
                return b;
            }
        }
        return end();
    }
};

#endif // BUFFER_HPP__

