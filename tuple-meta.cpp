#include <tuple>
#include <string>
#include <typeinfo>
#include <iostream>
#include <boost/static_assert.hpp>

template<typename Sequence, typename T> struct push_front;
template<template<typename...> class Sequence, typename T, typename ... Args>
struct push_front<Sequence<Args...>, T> {
    typedef Sequence<T, Args...> type;
};

template<typename Sequence> struct pop_front;
template<template<typename...> class Sequence, typename T, typename ... Args>
struct pop_front<Sequence<T, Args...>> {
    typedef Sequence<Args...> type;
};

template <typename Tp, typename... T> struct TVar_each;
template <typename Tp> struct TVar_each<Tp> { static void sf() {} };
template <typename Tp, typename A, typename... T>
struct TVar_each<Tp,A,T...> {
    BOOST_STATIC_ASSERT(std::tuple_size<Tp>::value==1+sizeof...(T));
    static void sf() {
        std::cout <<" "<< typeid(A).name();
        TVar_each<typename pop_front<Tp>::type,T...>::sf();
    }
};

template <typename Tp, typename... T> struct TVar_match;
template <typename Tp> struct TVar_match<Tp> : std::true_type {};
template <typename Tp, typename A, typename... T>
struct TVar_match<Tp,A,T...>
    : std::conditional<
        std::is_same<A,typename std::tuple_element<0,Tp>::type>::value
            , TVar_match<typename pop_front<Tp>::type,T...>
            , typename std::conditional<
                std::is_arithmetic<A>::value && std::is_convertible<A,typename std::tuple_element<0,Tp>::type>::value
                    , TVar_match<typename pop_front<Tp>::type,T...>
                    , std::false_type
                    >::type
            >::type {
    BOOST_STATIC_ASSERT(std::tuple_size<Tp>::value==1+sizeof...(T));
};

template <typename Tp, int N, int I=0> 
struct Tuple_each {
    static void sf(Tp tp) {
        std::cout <<" "<< typeid(typename std::tuple_element<I,Tp>::type).name();
        Tuple_each<Tp,N,I+1>::sf(tp); 
    }
};  
template <typename Tp, int N> struct Tuple_each<Tp,N,N> { static void sf(Tp) {} };

        template <size_t...> struct sum;
        template<size_t x> struct sum<x> {
            enum { value = x };
        };
        template<size_t x, size_t... y> struct sum<x, y...> {
            enum { value = x + sum<y...>::value }; 
        };

template<class... Types>
struct size_of {
    BOOST_STATIC_CONSTANT(int,value=sizeof...(Types));
};
// struct SS : std::string, std::string {};

//template <typename...T> struct All_convertible ; // : std::false_type {};
//template <typename...T>
//struct All_convertible<std::tuple<T...>> : All_convertible<T...>
//{};
//template <> struct All_convertible<> : std::true_type
//{};
//template <typename A, typename...T> 
//struct All_convertible<A,T...> : std::conditional< Convertible<A>::value //std::is_integral<A>::value
//        , typename All_convertible<T...>::type
//        , std::false_type
//    >::type
//{};
//
//template<typename... L>
//struct Zip {
//    template<typename... R>
//    struct With {
//        BOOST_STATIC_ASSERT(sizeof...(L)==sizeof...(R));
//        typedef std::tuple<std::pair<L,R>...> type;
//    };
//    template<typename... R>
//    struct With<std::tuple<R...>> : With<R...>
//    {};
//};
//template<typename... L> struct Zip<std::tuple<L...>> : Zip<L...> {};

template <typename...> struct Convertible ;
template <typename F, typename T>
struct Convertible<F,T>
        : std::is_convertible<F,T>
    //: std::conditional< std::is_same<F,T>::value
    //    , std::true_type
    //    , std::is_convertible<F,T>
    //    //, typename std::conditional< std::is_arithmetic<T>::value
    //    //    , std::is_convertible<F,T>
    //    //    , typename std::conditional< std::is_same<T,std::string>::value
    //    //        , std::is_same<char,typename std::remove_cv<typename std::remove_pointer<F>::type>>
    //    //        , std::false_type
    //    //    >::type
    //    //>::type
    //>::type
{};
template <typename F, typename T> struct Convertible<std::pair<F,T>> : Convertible<F,T> {};

template<typename FT, typename TT>
struct Tuple_convertible
    : std::conditional<Convertible<typename std::tuple_element<0,FT>::type,typename std::tuple_element<0,TT>::type>::value
        , typename Tuple_convertible<typename pop_front<FT>::type,typename pop_front<TT>::type>::type
        , std::false_type
    >::type
{};
template <> struct Tuple_convertible<std::tuple<>,std::tuple<>> : std::true_type {};
template <typename...T> struct Tuple_convertible<std::tuple<T...>,std::tuple<>> : std::false_type {};
template <typename...T> struct Tuple_convertible<std::tuple<>,std::tuple<T...>> : std::false_type {};

template <int,typename...> struct Match_result;
template <int I,typename Tp, typename First_type,typename Second_type>
struct Match_result<I,Tp,First_type,Second_type> : std::true_type
{
    BOOST_STATIC_CONSTANT(int,index=I);
    typedef First_type first_type;
    typedef Second_type second_type;
    typedef Tp from_type;
};
template <typename Tp>
struct Match_result<-1,Tp> : std::false_type
{
    BOOST_STATIC_CONSTANT(int,index=-1);
    typedef std::tuple<> first_type;
    typedef std::tuple<> second_type;
    typedef Tp from_type;
};

template <typename Tp, typename Tab, int N, int I=0> 
struct Table_index
{
    typedef typename std::tuple_element<I,Tab>::type Pair_type;
    typedef typename std::tuple_element<0,Pair_type>::type First_type;
    typedef typename std::tuple_element<1,Pair_type>::type Second_type;

    typedef typename std::conditional<Tuple_convertible<Tp,First_type>::value//std::tuple_size<Tp>::value != std::tuple_size<First_type>::value
            , Match_result<I,Tp,First_type,Second_type>
            , typename Table_index<Tp, Tab, N, I+1>::result_type
        >::type result_type;
};
template <typename Tp, typename Tab, int N> struct Table_index<Tp,Tab,N,N>
{ typedef Match_result<-1,Tp> result_type; };

template <typename Tab,typename Tp> 
using Table_index0 = typename Table_index<Tp,Tab,std::tuple_size<Tab>::value,0>::result_type;

typedef std::tuple<
    std::tuple<std::tuple<int>, std::tuple<int>>
    , std::tuple<std::tuple<bool,int>, std::tuple<int>>
    , std::tuple<std::tuple<bool,int,std::string>, std::tuple<int>>
    , std::tuple<std::tuple<int,std::string,long>, std::tuple<int>>
> table_t;

int main()
{
    typedef std::tuple<bool,int,std::string> Tp0;

    //typedef Zip<bool,int,std::string>::With<char,std::string,std::string>::type Zip0;
    //typedef Zip<long,int,std::string>::With<Tp0>::type Zip1;
    //typedef Zip<Tp0>::With<Tp0>::type Zip2;
    //std::cout <<"All_convertible:"
    //    <<"\n\t"<< All_convertible<Zip0>::value
    //    <<"\n\t"<< All_convertible<Zip1>::value
    //    <<"\n\t"<< All_convertible<Zip2>::value
    //    <<"\n";
    std::cout <<"Table_index0:"
        <<"\n\t"<< Table_index0<table_t, std::tuple<bool,short>>::index
        <<"\n\t"<< Table_index0<table_t, std::tuple<int,long,std::string>>::index
        <<"\n\t"<< Table_index0<table_t, std::tuple<int,std::string,bool>>::index
        <<"\n\t"<< Table_index0<table_t, std::tuple<int>>::index
        <<"\n\t"<< Table_index0<table_t, std::tuple<long long int>>::index
        <<"\n\t"<< Table_index0<table_t, std::tuple<int,char*,int>>::index
        <<"\n\t"<< Table_index0<table_t, std::tuple<int,char const* const,int const&>>::index
        <<"\n\t"<< Table_index0<table_t, std::tuple<int,int*,int>>::index
        <<"\n\t"<< Table_index0<table_t, std::tuple<bool const&>>::index
        <<"\n\t"<< Table_index0<table_t, std::tuple<char const&>>::index
        <<"\n";

    std::cout << std::tuple_size<Tp0>::value
        << " " << typeid(Tp0).name()
        << "\n";
    typedef pop_front<Tp0>::type Tp1;
    std::cout << std::tuple_size<Tp1>::value
        << " " << typeid(Tp1).name()
        << "\n";

    typedef pop_front<Tp0>::type Tp1;
    typedef pop_front<Tp1>::type Tp2;
    typedef pop_front<Tp2>::type Tp3;
    //typedef pop_front<Tp3>::type Tp4;

    Tp0 t;
    std::cout << std::tuple_size<Tp1>::value << " tuple_size\n";
    std::cout << size_of<bool,int>::value << " size_of\n";
    std::cout << size_of<>::value << " size_of\n";

    std::cout<<"TVar_each:\n";
    TVar_each<Tp0, bool,int,std::string>::sf(); std::cout<<"\n";
    TVar_each<Tp1, int,std::string>::sf(); std::cout<<"\n";;
    TVar_each<Tp2, std::string>::sf(); std::cout<<"\n";;
    TVar_each<Tp3>::sf(); std::cout<<"\n";;

    std::cout<<"Tuple_each:\n";
    Tuple_each<Tp0,std::tuple_size<Tp0>::value>::sf(Tp0()); std::cout<<"\n";
    Tuple_each<Tp1,std::tuple_size<Tp1>::value>::sf(Tp1()); std::cout<<"\n";;
    Tuple_each<Tp2,std::tuple_size<Tp2>::value>::sf(Tp2()); std::cout<<"\n";;
    Tuple_each<Tp3,std::tuple_size<Tp3>::value>::sf(Tp3()); std::cout<<"\n";;

    std::cout<<"TVar_match:\n";
    std::cout << TVar_match<Tp0, bool,int,std::string>::value <<"\n";
    std::cout << TVar_match<Tp0, bool,bool,std::string>::value <<"\n";
    std::cout << TVar_match<Tp0, bool,unsigned int,std::string>::value <<"\n";
    std::cout << TVar_match<Tp0, bool,char*,std::string>::value <<"\n";

    //std::cout << sizeof(t) << "\n";
}

