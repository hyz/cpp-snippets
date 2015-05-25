#include <tuple>
#include <typeinfo>
#include <type_traits>
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
    template <size_t N, typename A0> void dispatch_0_(A0* obj) { A0::template dispatch<N>(obj); }

    template<typename A0, size_t M, size_t Index>
    struct dispatch_fn_initializer : dispatch_fn_initializer<A0, M, Index+1>
    {
        typedef dispatch_fn_initializer<A0, M, Index+1> base;
        typedef void(*fn_type)(A0*);
        dispatch_fn_initializer(std::array<fn_type,M>& m)
            : base(m)
        {
            m[Index] = &dispatch_0_<Index, A0>;
        }
    };
    template<typename A0, size_t M>
    struct dispatch_fn_initializer<A0, M, M>
    {
        typedef void(*fn_type)(A0*);
        dispatch_fn_initializer(std::array<fn_type,M>&) {}
        void yes() {}
    };

} // namespace

template <typename Tab>
struct Pump
{
    typedef Pump this_type;
    typedef void(*disp_fn_type)(this_type*);

    std::array<disp_fn_type, std::tuple_size<Tab>::value> disp_fns_;
    //boost::asio::streambuf buf

    Pump() {
        detail::dispatch_fn_initializer<this_type,std::tuple_size<Tab>::value,0> init(disp_fns_);
        init.yes();
    };

    template <size_t X>
    static void dispatch(this_type* thiz)
    {
        typedef typename std::tuple_element<X,Tab>::type value_t;
        value_t val;
        std::cout <<"\ndispatch " << X <<" "<< typeid(value_t).name();
        //boost::asio::streambuf buf
    }
};

#include <typeinfo>
#include <iostream>
int main()
{
    std::tuple<char,float,int> t0;
    push_front<decltype(t0),std::string>::type t1;

    //std::cout <<"\n"<< tuple_index_<0,int, char,float,int,char,float>::type::index;
    std::cout <<"\n"<< tuple_index<decltype(t0),int>::value;
    std::cout <<"\n"<< tuple_index<decltype(t1),int>::value;

    //decltype(t1.get<0>()) fp;
    {
        Pump<decltype(t0)> pump;
        for (int i=0; i < std::tuple_size<decltype(t0)>::value; ++i) {
            pump.disp_fns_[i](&pump);
        }
    } {
        Pump<decltype(t1)> pump;
        for (int i=0; i < std::tuple_size<decltype(t1)>::value; ++i) {
            pump.disp_fns_[i](&pump);
        }
    }

    return 0;
}

