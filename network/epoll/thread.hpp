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
};

template <typename Object>
struct Thread : boost::noncopyable
{
    explicit Thread(Object& obj, /*void (Object::*run)(),*/ char const* sym=0)
        : obj_(&obj) {
        debugsym_ = (sym ? sym : "");
        stopped = created = detached = joined = 0;
    }
    ~Thread() {
        join();
    }

    void stop() {
        stopped = 1;
        DEBUG("%s %d:%s", debugsym_, __LINE__,__FUNCTION__);
        // if (has_thread_stop(obj_)) { obj_->thread_stop(); }
    }
    int detach() {
        detached = 1;
        return pthread_detach(pthread_);
    }

    int join(void** retval = 0)
    {
        if (!created || detached || joined) {
            DEBUG("%s %d:%s not joinable %d %d %d", debugsym_, __LINE__,__FUNCTION__,int(created),int(detached),int(joined));
            return 0;
        }
        DEBUG("%s %d:%s stopped %d", debugsym_, __LINE__,__FUNCTION__, int(stopped));
        int ec = pthread_join(pthread_, retval);
        if (ec) {
            ERR_EXIT("%s %d:%s pthread_join", debugsym_, __LINE__,__FUNCTION__);
        }
        joined = 1;
        DEBUG("%s %d:%s [OK]", debugsym_, __LINE__,__FUNCTION__);
        return 0;
    }

    int start() {
        int ec = pthread_create(&pthread_,NULL, &sfun, this); //pthread_self;
        if (ec) {
            ERR_EXIT("%s %d:%s pthread_create", debugsym_, __LINE__,__FUNCTION__);
        }
        created = 1;
        DEBUG("%s %d:%s [OK]", debugsym_, __LINE__,__FUNCTION__);
        return 0; //pthread_;
    }

    bool stopped, created, detached, joined;

private:
    Object* obj_;
    pthread_t pthread_;
    char const* debugsym_;

private: //typedef void (Object::*objrun_t)();
    static void* sfun(void* a) {
        static_cast<Thread*>(a)->obj_->run();
        return NULL;
    }
};

//template <typename RunnableObject> struct Thread : boost::noncopyable { };

#endif // THREAD_HPP__

