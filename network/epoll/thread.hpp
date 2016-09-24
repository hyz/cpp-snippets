#ifndef THREAD_HPP__
#define THREAD_HPP__

#include <time.h>
#include <pthread.h>
#include <boost/noncopyable.hpp>
#include "alog.hpp"

struct pthread_mutex_type : boost::noncopyable
{
    pthread_mutex_t mutex_;
    pthread_mutex_type() {
        pthread_mutex_init(&mutex_, 0);
    }
    ~pthread_mutex_type() {
        pthread_mutex_destroy(&mutex_);
    }
    int lock() {
        return pthread_mutex_lock(&mutex_);
    }
    int unlock() {
        return pthread_mutex_unlock(&mutex_);
    }
};
struct pthread_mutex_lock_guard : boost::noncopyable
{
    pthread_mutex_type* mutex_;
    pthread_mutex_lock_guard(pthread_mutex_type& m) : mutex_(&m) {
        mutex_->lock();
    }
    ~pthread_mutex_lock_guard() {
        mutex_->unlock();
    }
};
struct pthread_cond_type : boost::noncopyable
{
    pthread_cond_t cond_;
    pthread_cond_type() {
        pthread_cond_init(&cond_, NULL);
    }
    ~pthread_cond_type() {
        pthread_cond_destroy(&cond_);
    }
    int wait(pthread_mutex_type& m) {
        return pthread_cond_wait(&cond_, &m.mutex_);
    }
    int wait(pthread_mutex_type& m, unsigned microsec) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 1000*microsec;
        return pthread_cond_timedwait(&cond_, &m.mutex_, &ts);
    }
    int signal() {
        return pthread_cond_signal(&cond_);
    }
    int broadcast() {
        return pthread_cond_broadcast(&cond_);
    }
    int notify_one() {
        return pthread_cond_signal(&cond_);
    }
    int notify_all() {
        return pthread_cond_broadcast(&cond_);
    }
};

template <typename ValueType>
struct BitMask {
    BitMask() : val_() {}

    void set(unsigned x, bool yn) {
        if (yn)
            val_ |= ValueType(1<<x);
        else
            val_ &= ~ValueType(1<<x);
    }
    bool test(unsigned x) const { return bool(mask(x)); }
    ValueType mask(unsigned x) const { return (val_ & ValueType(1<<x)); }

    void reset() { val_ = ValueType(); }
    void reset(ValueType val) { val_ = val; }

    ValueType val_;
};

template <typename Object>
struct Thread : boost::noncopyable
{
    explicit Thread(Object& obj, /*void (Object::*run)(),*/ char const* sym=0)
        : obj_(&obj)
    {
        debugsym = (sym ? sym : "");
        stopped = 0; //created = detached = joined = 0;
    }
    ~Thread() {
        join();
    }

    void stop() {
        stopped = 1; // msk_.set(Xstopped, 1)
        DEBUG("%s:%u %u", debugsym, (unsigned)pthread_, (unsigned)pthread_self());
    }
    int detach() {
        msk_.set(Xdetached, 1); // detached = 1;
        return pthread_detach(pthread_);
    }

    int join(void** retval = 0)
    {
        if (!msk_.test(Xcreated) || msk_.test(Xdetached) || msk_.test(Xjoined)) { //(!created || detached || joined)
            int created = msk_.test(Xcreated);
            int detached = msk_.test(Xdetached);
            int joined = msk_.test(Xjoined);
            DEBUG("%s not joinable: c %d d %d j %d", debugsym, created,detached,joined);
            return 0;
        }
        DEBUG("%s stopped=%d", debugsym, int(stopped));
        int ec = ::pthread_join(pthread_, retval);
        if (ec) {
            ERR_EXIT("%s pthread_join", debugsym);
        }
        msk_.set(Xjoined, 1); // joined = 1;
        DEBUG("%s:%u [OK]", debugsym, unsigned(pthread_));
        return 0;
    }

    int start() {
        int ec = ::pthread_create(&pthread_,NULL, &sfun, this); //pthread_self;
        if (ec) {
            ERR_EXIT("%s pthread_create", debugsym);
        }
        msk_.set(Xcreated, 1); // created = 1;
        //gettid();
        DEBUG("%s:%u [OK]", debugsym, unsigned(pthread_));
        return 0; //pthread_;
    }

    //TODO: movable

    char const* debugsym;
    bool stopped; //, created, detached, joined;
private:
    enum { Xcreated=1, Xdetached, Xjoined };
    BitMask<unsigned char> msk_;
    Object* obj_;
    pthread_t pthread_;

private: //typedef void (Object::*objrun_t)();
    static void* sfun(void* a) {
        static_cast<Thread*>(a)->obj_->run();
        return NULL;
    }
};

//template <typename RunnableObject> struct Thread : boost::noncopyable { };

#endif // THREAD_HPP__

