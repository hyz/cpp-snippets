#include <tuple>
#include <typeinfo>
#include <type_traits>
#include <boost/static_assert.hpp>
#include <boost/mpl/if.hpp>
#include <boost/mpl/not.hpp>
#include <boost/mpl/vector.hpp>

namespace mpl = boost::mpl;

template<typename Sequence, typename T>
struct push_front;

template<template<typename...> class Sequence, typename T, typename ... Args>
struct push_front< Sequence<Args...>,T> {
    typedef Sequence<T, Args...> type;
};

template<template<typename...> class To, typename From> struct tuple_change;

template<template<typename...> class To, template<typename...> class From, typename ... Args>
struct tuple_change<To, From<Args...>>
{
    typedef To<Args...> type;
};

template<typename Sequence, size_t N>
struct at : std::tuple_element<N,Sequence> { };

template<typename Sequence>
struct empty;

template<template<typename...> class Sequence, typename ... Args>
struct empty<Sequence<Args...>> {
    typedef Sequence<> type;
};

template<
    size_t N,
    typename Sequence,
    template<typename> class Pred,
    typename ... Args >
struct while_impl
{
    typedef typename mpl::if_c<
      Pred<
          typename at<Sequence, sizeof...(Args) - N -1>::type
      >::value,
      typename push_front<
          typename while_impl<N-1, Sequence, Pred, Args...>::type, 
              typename at<Sequence,sizeof...(Args)-N-1>::type
      >::type,
      typename empty< Sequence > ::type
    >::type type;
};

template<
  typename Sequence,
  template<typename> class Pred,
  typename ... Args >
struct while_impl<-1, Sequence, Pred, Args...>
: empty<Sequence> {
};


template<
  typename Sequence,
  template<typename> class Pred>
struct while_;

template<
  template<typename...> class Sequence,
  template<typename> class Pred,
  typename ... Args >
struct while_< Sequence<Args...>, Pred >
{
  typedef typename while_impl<sizeof...(Args)-1, Sequence<Args...>, Pred, Args...>::type type;
};

template<typename T>
struct not_na : mpl::not_< std::is_same<mpl_::na, T> >
{ };

template<template<typename...> class To, typename From>
struct to_boost;

template<template<typename...> class To, typename...Args >
struct to_boost<To, std::tuple<Args...> > :
  tuple_change< mpl::vector, std::tuple<Args...> >
{ };

template< typename From >
struct to_std;

template<template<typename...> class From, typename...Args >
struct to_std< From<Args...> > :
   while_<typename tuple_change< std::tuple, From<Args...> >::type, not_na>
{ };

static_assert(
std::is_same<
    mpl::vector< char, int, bool>,
    typename to_boost<mpl::vector, std::tuple<char, int, bool> >::type
  >::value,
"tuple_change to boost failed");

static_assert(
  std::is_same<
    std::tuple< char, int, bool>,
    typename to_std< mpl::vector<char, int, bool> >::type
  >::value,
"tuple_change from boost failed");

    //template<int...S> struct seq {
    //    template <typename F, typename T> static inline void invoke(F&& f, T&& t) { f(std::get<S>(t)...); }
    //};
    //template<int N, int ...S> struct gens : gens<N-1, N-1, S...> {};
    //template<int ...S> struct gens<0, S...> { typedef seq<S...> type; };

//    template <typename T, int N>
//    struct Invoker : Invoker<N-1,T>
//    {
//        void des(T& t, std::istream& ins)
//        {
//            std::tuple_element<N-1,T>::type& elem = std::get<N-1>(t);
//            elem, ins;
//            Invoker<N-1,T>::des(t, ins)
//        }
//        void invoke(T& t) {
//            ;
//        }
//        void operator()(std::istream& ins) {
//            T t
//            des(t, ins);
//            invoke(t);
//        }
//    };
//
//    template <typename T>
//    struct Invoker<0> {
//        void des(T& t, std::istream& ins) {}
//    };
//
//    void process(int idx, std::istream& ins)
//    {
//        (*ptab_[idx])(ins);
//    }
//
//    template <typename T> void process_(std::istream& ins)
//    {
//        Invoker<sizeof...(T), T> ivk;
//        ivk(ins);
//        // std::tuple_element<X,T>::type;
//    }
//    // template <typename... T> using remove_ref_tuple = std::tuple<typename std::remove_reference<T>::type...>;

#include<tuple>
#include<type_traits>
#include<string>
#include<iostream>
//#include <boost/archive/text_iarchive.hpp>
//#include <boost/archive/text_oarchive.hpp>
//typedef boost::archive::text_oarchive oArchive;
//typedef boost::archive::text_iarchive iArchive;
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
typedef boost::archive::binary_oarchive oArchive;
typedef boost::archive::binary_iarchive iArchive;

namespace detail {
    template<int Index, class Search, class First, class... Types>
    struct tuple_index_helper
    {
        typedef typename tuple_index_helper<Index+1, Search, Types...>::type type;
    };
    template<int Index, class Search, class... Types>
    struct tuple_index_helper<Index, Search, Search, Types...>
    {
        typedef tuple_index_helper type;
        static constexpr int index = Index;
    };
} // namespace

template<typename Sequence, typename Search>
struct tuple_index;

template<template<typename...> class Sequence, typename Search, typename... Types>
struct tuple_index<Sequence<Types...>,Search>
{
    static constexpr int value = detail::tuple_index_helper<0,Search,Types...>::type::index;
};

// template<typename Sequence, typename T>
// struct push_front;
// template<template<typename...> class Sequence, typename T, typename ... Args>
// struct push_front< Sequence<Args...>,T> {
//     typedef Sequence<T, Args...> type;
// };

#include <array>

namespace detail {
    //template <int N, typename A0> void dispatch_0_(A0* obj) { A0::template dispatch<N>(obj); }
    //template<typename A0, int M, int Index>
    //struct dispatch_fn_initializer : dispatch_fn_initializer<A0, M, Index+1>
    //{
    //    typedef dispatch_fn_initializer<A0, M, Index+1> base;
    //    typedef void(*fn_type)(A0*);
    //    dispatch_fn_initializer(std::array<fn_type,M>& m)
    //        : base(m)
    //    {
    //        m[Index] = &dispatch_0_<Index, A0>;
    //    }
    //};
    //template<typename A0, int M> struct dispatch_fn_initializer<A0, M, M>
    //{
    //    typedef void(*fn_type)(A0*);
    //    dispatch_fn_initializer(std::array<fn_type,M>&) {}
    //    void yes() {}
    //};

    template<int... S> struct seq { static constexpr int length = sizeof...(S); };
    template<int N, int... S> struct gens : gens<N-1, N-1, S...> {};
    template<int... S> struct gens<0, S...> { typedef seq<S...> type; };

    template<typename> struct invoke;
    template<template<int...> class seq, int... Indices>
    struct invoke<seq<Indices...>> {
        template <typename H,typename T>
        void call(H& h, T& t) const { h(std::get<Indices>(t)...); }
    };

    // template<typename> struct invoke;
    // template<template<int...> class seq, int... Indices>
    // struct invoke<seq<Indices...>> {
    //     template <typename H,typename T>
    //     void call(H& h, T& t) const { h(std::get<Indices>(t)...); }
    // };

    //template<int... Indices>
    //struct indices {
    //    typedef indices<Indices..., sizeof...(Indices)> next;
    //};
    //template<int N>
    //struct build_indices {
    //    typedef typename build_indices<N-1>::type::next type;
    //};
    //template<>
    //struct build_indices<0> {
    //    typedef indices<> type;
    //};

/// } // namespace namespace { //////////////////////////////

    template<typename,typename> struct Lookup;
    template<typename Tp, template<int...> class seq, int... Indices>
    struct Lookup<Tp,seq<Indices...>>
    {
        template <int X> static void gcall_(Tp* thiz, iArchive& ar) { thiz->template callback<X>(ar); }
        void call(int i, Tp* thiz, iArchive& ar) const { (*lookup[i])(thiz, ar); }
        void(*lookup[sizeof...(Indices)])(Tp*,iArchive&) = { &Lookup::gcall_<Indices>... };
        //static constexpr std::array<void(*)(Tp*),sizeof...(Indices)> tab = { &dispatch_g<Indices,Tp>... };
    };
} // namespace ////////////////////////////

namespace variadic {
    namespace detail {
        //template <typename T, typename... Ts>
        //struct Serialize : Serialize<Ts...> {
        //    template <typename A> static void sf(A & ar, T&& x, Ts&&...ts) {
        //        BOOST_STATIC_ASSERT(!std::is_pointer<typename std::remove_reference<T>::type>::value);
        //        ar << x;
        //        Serialize<Ts...>::template sf(ar, std::forward<Ts>(ts)...);
        //    }
        //};
        //template <typename T>
        //struct Serialize<T> {
        //    template <typename A> static void sf(A & ar, T&& x) {
        //        BOOST_STATIC_ASSERT(!std::is_pointer<typename std::remove_reference<T>::type>::value);
        //        ar << x;
        //    }
        //};
    } // namespace detail

    template<typename Archive, typename... T>
    void serialize(Archive & ar, T&&... t)
    {
        //detail::Serialize<T...>::template sf(ar, std::forward<T>(ts)...);
        using swallow = int[];
        (void)swallow{0, (void(ar & t), 0)...};
    }

    namespace detail {
        template <typename> struct serialize_helper;
        template <template<int...> class seq, int... Indices>
        struct serialize_helper<seq<Indices...>> {
            template <typename A, typename T> static void ssf(A & ar, T& t) {
                serialize(ar, std::get<Indices>(t)...);
            }
        };
    } // namespace detail

    template<typename Archive, typename Tuple>
    void serialize_tuple_unpack(Archive& ar, Tuple & tux)
    {
        detail::serialize_helper<typename ::detail::gens<std::tuple_size<Tuple>::value>::type>::template ssf(ar, tux);
    }
} // namespace variadic

namespace boost { namespace serialization {
    namespace tuple_ {
        //template<int M, int N>
        //struct serialize_ {
        //    template<class Archive, typename... Ts>
        //    static void sf(Archive & ar, std::tuple<Ts...> & t, unsigned int version)
        //    {
        //        ar & std::get<N>(t);
        //        //std::cout << typeid(decltype(std::get<N>(t))).name() << "\n";
        //        //std::cerr <<"#"<< std::get<N>(t) <<"#"<<M<<"#"<<N<<"#\n";
        //        serialize_<M, N+1>::template sf(ar, t, version);
        //    }
        //};
        //template<int M>
        //struct serialize_<M, M> {
        //    template<class Archive, typename... Ts>
        //    static void sf(Archive & ar, std::tuple<Ts...>&, unsigned int)
        //    {}
        //};
    } // namespace tuple_

    //template<class Archive, typename... Ts>
    //void serialize(Archive & ar, std::tuple<Ts...> & t, unsigned int version)
    //{
    //    variadic::serialize_tuple_unpack(ar, t);
    //    //tuple_::serialize_<std::tuple_size<std::tuple<Ts...>>::value, 0>::template sf(ar, t, version);
    //}
    // forward_as_tuple;
    // template<typename... Ts> struct as_tuple;
} } // namespace // boost // serialization
struct serializable0
{
    template<class Archive> void serialize(Archive &, unsigned int) {}
};

#include <list>
#include <forward_list>
#include <boost/asio/streambuf.hpp>

template <typename Tab,typename... Args>
using Message_index = tuple_index<Tab,std::tuple<typename std::decay<Args>::type...>>;

template <class Tab, class Handle>
struct Pump : detail::Lookup<Pump<Tab,Handle>,typename detail::gens<std::tuple_size<Tab>::value>::type>
{
    typedef detail::Lookup<Pump<Tab,Handle>,typename detail::gens<std::tuple_size<Tab>::value>::type> Lookup;
    typedef Pump this_type;

    Handle handle_;

    //struct head5 // std::aligned_union<sizeof(union_head_t),alignof(uint32_t)>::type
    //{
    //    union union_head5_ {
    //        uint32_t len_;
    //        uint8_t bytes_[5];
    //    } uh;
    //    static constexpr int size=5;
    //    void* address() { return static_cast<void*>(&uh.bytes_[0]); }
    //    uint32_t length() const { return uh.len_; }
    //    void length(uint32_t n) { uh.len_ = n; }
    //    uint8_t type() const { return uh.bytes_[4]; }
    //    void type(uint8_t n) { uh.bytes_[4] = n; }
    //};

    struct recv_buffer : boost::asio::streambuf {
        uint32_t len = 0;
    } recv_buf;

    struct buffer_list : private std::forward_list<boost::asio::streambuf>
    {
        typedef std::forward_list<boost::asio::streambuf> base;
        iterator before_end_;

        buffer_list() {
            before_end_ = before_begin();
        }
        template <typename... A>
        iterator emplace_back(A&&... a) {
            return (before_end_ = emplace_after(before_end_, std::forward<A>(a)...));
        }
        void pop_front() {
            base::pop_front();
            if (empty()) {
                before_end_ = before_begin();
            }
        }
        using base::emplace_front;
        using base::front;
        using base::empty;
    };
    struct send_buffers : buffer_list {
        uint32_t len = 0;
    } send_bufs;

    //template <typename... T> using Args_decay = std::tuple<typename std::decay<T>::type...>;
    template <typename... T>
    void async_send(T&&... l)
    {
        typedef std::tuple<typename std::decay<T>::type...> Tuple;
        uint8_t tyidx = static_cast<uint8_t>(tuple_index<Tab,Tuple>::value);
        std::cout <<"\nsend " << int(tyidx);

        bool empty = send_bufs.empty();
        auto it = send_bufs.emplace_back(); {
            oArchive ar(*it, boost::archive::no_header);
            ar & tyidx;
            variadic::serialize(ar, std::forward<T>(l)...);
        }
        if (empty) {
            send_bufs.len;
            //async_write;

            if (1) { // test
                {
                    std::ostream out(&recv_buf);
                    out << &(*it); // recv_buf.rdbuf(it->rdbuf()); // = std::move(*it);
                }
                send_bufs.pop_front();
                this->call0();
            }
        }
    }

    void call0()
    {
        iArchive ar(recv_buf, boost::archive::no_header);
        uint8_t tyidx;
        ar >> tyidx;
        std::cout <<"\ndispatch " << int(tyidx); // <<" "<< typeid(Tuple).name();
        Lookup::call(tyidx, this, ar);
    }

    template <int X> void callback(iArchive & ar)
    {
        typedef typename std::tuple_element<X,Tab>::type Tuple;

        Tuple tux; {
            variadic::serialize_tuple_unpack(ar, tux);
        }

        detail::invoke<typename detail::gens<std::tuple_size<Tuple>::value>::type> ivk;
        ivk.call(handle_, tux);
    }

};

#include <typeinfo>
#include <iostream>

struct Id : serializable0 {};

typedef std::tuple<
          std::tuple<char>
        , std::tuple<short>
        , std::tuple<int>
        , std::tuple<long>
        , std::tuple<float>
        , std::tuple<double>
        , std::tuple<std::string>
        , std::tuple<int, std::string>
        , std::tuple<Id, int, std::string>
    > Message_table;

template <typename Tab>
struct Message_handle_
{
    template <typename... T> void print(std::ostream& out, T&&... x) {
        using swallow = int[];
        (void)swallow{ (void(out<<x<<" "),0)... };
    }

    template <typename... T>
    void operator()(T&&... t) {
        std::cout <<"\nnot handled " << Message_index<Tab,T...>::value
            << ": ...<";
        print(std::cout, std::forward<T>(t)...);
        std::cout << ">\n";
    }

    void operator()(char c) {
        std::cout <<"\nchar " << int(c);
    }
    void operator()(int i, std::string& s) {
        std::cout <<"\nint string: " <<i<<" "<<s;
    }
    void operator()(Id, int i, std::string& s) {
        std::cout <<"\nId int string: " <<i<<" "<<s;
    }
};
struct Message_handle : Message_handle_<Message_table> {};

int main()
{
    using namespace std;

    Pump<Message_table, Message_handle> pump;

    pump.async_send(char(40));
    pump.async_send(int(65));
    pump.async_send(float(2.2345));
    pump.async_send(double(1.12345));
    pump.async_send(int(11), string("google.com"));
    pump.async_send(Id{}, int(111), string("hotmail.com"));

    for (int i=0; i < tuple_size<Message_table>::value; ++i) {
        //pump.call0();
    }

    return 0;
}

