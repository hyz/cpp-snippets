#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <chrono>
#include <thread>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio.hpp>

#if defined(BOOST_MSVC)
#include < time.h >
#include < windows.h >

struct timezone
{
	int  tz_minuteswest; /* minutes W of Greenwich */
	int  tz_dsttime;     /* type of dst correction */
};

int gettimeofday(struct timeval *tv, struct timezone *tz);
#endif

namespace ip = boost::asio::ip;
using ip::udp;

inline void exit127_(int,int) { exit(127); }
#define ERR_EXIT(...) exit127_( fprintf(stderr, __VA_ARGS__), fprintf(stderr, " error:%d: %s\n",__LINE__, strerror(errno)) )
#define ERR_MSG(...) (void)( fprintf(stderr, __VA_ARGS__), fprintf(stderr, " error:%d: %s\n",__LINE__, strerror(errno)) )
#define DBG_MSG(...) (void)( fprintf(stderr, __VA_ARGS__), fprintf(stderr, " debug:%d\n",__LINE__) )

struct Main : boost::asio::io_service
{
    Main(udp::endpoint ep, int Nps, int Kps)
        : deadline_(io_service())//(, boost::posix_time::pos_infin)
        , socket_(io_service(), udp::endpoint(udp::v4(),0))
        , endpoint_(ep)
    {
        singleton = this;
        gettimeofday(&tv0_, NULL);

        numbps_ = numbpx_ = Nps;
        numbrush_ = numbrx_ = 0;
        bytes1_ = std::max(Kps*1024/Nps, 1500);

        deadline_.expires_from_now(boost::posix_time::milliseconds(0));
        deadline_.async_wait( [this](boost::system::error_code){do_send();} );
        printf("sending ...\n");
    }
    Main(short bind_port)
        : deadline_(io_service())
        , socket_(io_service(), udp::endpoint(udp::v4(), bind_port))
    {
        singleton = this;
        gettimeofday(&tv0_, NULL);
        post( [this](){do_receive();} );
        // deadline_.expires_from_now(boost::posix_time::pos_infin);
        printf("receiving ...\n");
    }
    ~Main() {}

    void run() {
        srand(time(0));
        boost::asio::io_service::run();
    }

    boost::asio::io_service& io_service() { return *this; }
private:
    struct timeval tv0_;
    static unsigned sMills() {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return (1000*1000*(tv.tv_sec - singleton->tv0_.tv_sec) + tv.tv_usec - singleton->tv0_.tv_usec)/1000;
    }

    struct recv_st
    {
        unsigned seq = 0;
        unsigned n_dgram = 0;
        unsigned bytes_recvd = 0;
        unsigned bytes_exped = 0;

        void incoming(unsigned len, unsigned seq, unsigned nbytes) {
			if (n_dgram == 0)
				gettimeofday(&Main::singleton->tv0_, NULL);
            recv_st prev = *this;

            this->n_dgram++;
            this->bytes_exped += len;
            this->seq = seq;
            this->bytes_recvd += nbytes;

            this->print(prev);
        }

        void print(recv_st const& prev) const {
            char const* seqv = 0;
            if (seq != prev.seq+1) {
                char const* legv[3] = { "misorder", "repeated", "lost" };
                seqv = legv[(seq<prev.seq ? -1 : (seq==prev.seq ? 0 : 1)) +1];
            }

            unsigned ct = Main::sMills();
            if (seqv || ((ct - tpr_) > 5 && (seq&0x7f)==0) || (seq&0x7ff) == 0 || seq<3) {
                int nsec = (ct)/1000;
                if (nsec == 0)
                    nsec = 1;
                printf("%3u.%03u %4u %-4u %-4u %6.1f %7.1f %u%c", ct/1000,ct%1000
                        , prev.seq, seq, seq+1-n_dgram, float(n_dgram)/nsec, bytes_recvd/1024.0/nsec, bytes_recvd, seqv?' ':'\n');
                if (seqv)
                    printf("* %s\n", seqv);//("* %u a %u\n", bytes_recvd-prev.bytes_recvd, bytes_recvd);
                tpr_=ct;
            }
        }
        
        mutable unsigned tpr_ = 0;
    } recv_;

    struct sent_st {
        unsigned seq = 0;
        unsigned bytes_sent = 0;

        void outgoing(unsigned, unsigned bytes) {
            sent_st const& prev = *this;
            seq++;
            bytes_sent += bytes;
            this->print(prev);
        }
        //unsigned numb_psec_ = 30;
        //unsigned bytes_psec_ = 1024*300;

        void print(sent_st const&) const {
            unsigned ct = Main::sMills();
            int nsec = ct/1000;
            if (nsec == 0)
                nsec = 1;
            if (((ct - tpr_) > 5 && (seq&0x7f)==0) || (seq&0x7ff) == 0 || seq < 3) {
                printf("%3u.%03u %4u %4.1f %4.1f %u\n", ct/1000,ct%1000, seq, float(seq+1)/nsec, bytes_sent/1024.0/nsec, bytes_sent);
                tpr_ = ct;
            }
        }

        mutable unsigned tpr_;
    } sent_;

private:
    unsigned char numbps_, numbpx_, numbrush_, numbrx_;
    unsigned short bytes1_;

    void do_receive()
    {
        socket_.async_receive_from( boost::asio::buffer((void*)data_, max_length), endpoint_
            , [this](boost::system::error_code ec, std::size_t bytes_recvd) {
                if (!ec && bytes_recvd >= 16) {
                    recv_.incoming(ntohl(data_[0]), ntohl(data_[1]), bytes_recvd);
                    do_receive();
                } else {
                    ERR_EXIT("%d:%s", ec.value(), ec.message().c_str());
                }
            });
    }

    void do_send()
    {
        if (deadline_.expires_at() > boost::asio::deadline_timer::traits_type::now()) {
            return;
        }
        if (numbrx_ >= numbrush_ && numbpx_ >= numbps_) {
            numbrx_ = numbpx_ = 0;
            numbrush_ = 3+rand()%3;
        }

        int wmills = 1000/(numbps_+1);//int raval = rand()%100;
        unsigned length = unsigned(bytes1_) + rand()%3000 - 1500; //= 1024*( (raval>90?15:5) + raval%5);
        if (numbrx_ < numbrush_) {
            numbrx_++;
            wmills = 0;
            //length = length * (1 + rand()%200/100.0);
        } else if (numbpx_ < numbps_) {
            numbpx_++;
        }
        if (length > 1024*56) {
            DBG_MSG("%u > 56K", length/1024);
            length = std::min(56*1024, int(length));
        }

        data_[0] = htonl(length);
        data_[1] = htonl(sent_.seq);
        //data_[2] = htonl(1);

        socket_.async_send_to( boost::asio::buffer((void*)data_, length), endpoint_
            , [this,wmills](boost::system::error_code ec, std::size_t bytes_sent) {
                  if (!ec) {
                      sent_.outgoing(sent_.seq, bytes_sent); // sent_.print();

                      deadline_.expires_from_now(boost::posix_time::milliseconds(wmills));
                      deadline_.async_wait( [this](boost::system::error_code){do_send();} ); //(boost::bind(&Main::do_send, this));
                  } else {
                      ERR_EXIT("%d:%s", ec.value(), ec.message().c_str());
                  }
            });
    }

private:
    boost::asio::deadline_timer deadline_;
    udp::socket socket_;
    udp::endpoint endpoint_;

    enum { max_length = 1024*128 };
    int32_t data_[max_length/4];
    static Main* singleton;
};
Main* Main::singleton;

int main(int argc, char* argv[])
{
    try {
        if (argc == 5) {
            Main sender(udp::endpoint(ip::address::from_string(argv[1]), std::atoi(argv[2]))
                    , atoi(argv[3]), atoi(argv[4]));
            sender.run();
        } else if (argc == 2) {
            Main receiver(std::atoi(argv[1]));
            receiver.run();
        } else {
            fprintf(stderr,"Usage: \n"
                    "  send:\ta.out <host> <port> <Npkg/s> <KB/s>\n"
                    "  recv:\ta.out <port>\n");
            return 1;
        }
    } catch (std::exception const& e) {
        fprintf(stderr,"Exception: %s\n", e.what());
    }

    return 0;
}

#if defined(BOOST_MSVC)
#include < time.h >
#include < windows.h >

//struct timezone
//{
//	int  tz_minuteswest; /* minutes W of Greenwich */
//	int  tz_dsttime;     /* type of dst correction */
//};
//
//int gettimeofday(struct timeval *tv, struct timezone *tz);


#if defined(_MSC_VER) || defined(_MSC_EXTENSIONS)
#define DELTA_EPOCH_IN_MICROSECS  11644473600000000Ui64
#else
#define DELTA_EPOCH_IN_MICROSECS  11644473600000000ULL
#endif

int gettimeofday(struct timeval *tv, struct timezone *tz)

{
	FILETIME ft;
	unsigned __int64 tmpres = 0;
	static int tzflag = 0;

	if (NULL != tv)
	{
		GetSystemTimeAsFileTime(&ft);

		tmpres |= ft.dwHighDateTime;
		tmpres <<= 32;
		tmpres |= ft.dwLowDateTime;

		tmpres /= 10;  /*convert into microseconds*/
					   /*converting file time to unix epoch*/
		tmpres -= DELTA_EPOCH_IN_MICROSECS;
		tv->tv_sec = (long)(tmpres / 1000000UL);
		tv->tv_usec = (long)(tmpres % 1000000UL);
	}

	if (NULL != tz)
	{
		if (!tzflag)
		{
			_tzset();
			tzflag++;
		}
		tz->tz_minuteswest = _timezone / 60;
		tz->tz_dsttime = _daylight;
	}

	return 0;
}

#endif
