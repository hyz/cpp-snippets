#ifndef BUFFER_HPP__
#define BUFFER_HPP__

#include <stdlib.h>
#include <algorithm>
#include <boost/range.hpp>
#include "alog.hpp"
#include "thread.hpp"

struct buffer_ref : boost::iterator_range<char*>
{
    typedef boost::iterator_range<char*> range;

    unsigned capacity() const { return size_fr(begin()); }

    range spaces() const { return boost::iterator_range<char*>(end(), end_); }

    void commit(range& sub, unsigned len) {
        if (&sub != this
                && (sub.begin() != end() || len > size_fr(end()) || sub.size() != size_fr(end()))) {
            ERR_EXIT("buffer_ref:commit %u %u", len, sub.size());
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
template <unsigned Capacity>
struct malloc_buf : buffer_ref
{
    ~malloc_buf() {
        if (begin()) {
            ::free(begin());
            buffer_ref& b = *this;
            b = buffer_ref();
        }
    }
    malloc_buf()
        : buffer_ref((char*)::malloc(Capacity))
    { end_ = begin() + Capacity; }
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

    enum { buffer_size = BufSiz };
    enum { Bufcount = 2 };
    statful_buf bufs_[Bufcount];
    pthread_mutex_type mutex_;
    pthread_cond_type cond_;

    typedef statful_buf* iterator;
    iterator ip_;

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
        ip_ = begin();
    }

    iterator alloc(unsigned expsize) {
        if (expsize > BufSiz)
            ERR_EXIT("alloc:size");
        pthread_mutex_lock_guard lk(mutex_);
        //DEBUG("buf:alloc: %u %d", ip_-begin(), ip_->stat);
        if (ip_->stat == EUsing) {
            ip_ = incr(ip_);
            if (ip_->stat == EUsing)
                ERR_EXIT("stat:using");
        //DEBUG("buf:using:alloc: %u %d", ip_-begin(), ip_->stat);
        }
        ip_->stat = EAlloc;
        ip_->consume(ip_->size());
        return ip_;
    }

    iterator wait(unsigned millis) {
        pthread_mutex_lock_guard lk(mutex_);
        iterator j = findstat(EUsable, ip_, decr(ip_));
        if (j->stat != EUsable) {
            cond_.wait(mutex_, millis);
            j = findstat(EUsable, ip_, decr(ip_));
            if (j->stat != EUsable)
                return end();
        }
        //DEBUG("buf:wait: %u %d", j-begin(), j->stat);
        j->stat = EUsing;
        return j;
    }

    void done(iterator j) {
        if (j->stat == EAlloc) {
            if (j != ip_)
                ERR_EXIT("done %p %p", j, ip_);
            //pthread_mutex_lock_guard lk(mutex_);
            ip_ = incr(j);
            j->stat = EUsable;
            //DEBUG("done EAlloc %u", j-begin());
            cond_.signal();
        } else if (j->stat == EUsing) {
            j->stat = 0;
            //DEBUG("done EUsing %u", j-begin());
        } else {
            ERR_EXIT("buf:stat");
        }
    }

    bool empty() const { return false; }
    unsigned size() const { return Bufcount; }
    iterator end() { return begin()+size(); }
private:
    iterator begin() { return &bufs_[0]; }
    iterator last() { return end()-1; }
    inline iterator incr(iterator i) { return ((++i == end()) ? begin() : i); }
    inline iterator decr(iterator i) { return ((i == begin()) ? last() : --i); }

    //iterator findstat_r(unsigned xe, iterator p, iterator ep) {
    //    while (p != ep && p->stat != xe) {
    //        p = decr(p);
    //    }
    //    return p;
    //}
    iterator findstat(unsigned xe, iterator p, iterator ep) {
        while (p != ep && p->stat != xe) {
            p = incr(p);
        }
        return p;
    }
};

#endif // BUFFER_HPP__

