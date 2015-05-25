#include <stdint.h>
#include <string>
#include <vector>
#include <type_traits>
#include <typeinfo>
#include <iostream>
#include <boost/static_assert.hpp>
#include <boost/variant/variant.hpp>
#include <boost/container/flat_map.hpp>

namespace json {
//typedef boost::none_t null_t;
typedef bool bool_t;
typedef double float_t;
typedef int64_t int_t;
typedef std::string string;
}

namespace isX {
    template <typename T> struct is_bool : std::false_type {};
    template <> struct is_bool<bool> : std::true_type {};

    template <typename T> 
    struct inner_type {
        typedef typename std::conditional<is_bool<T>::value
            , bool
            , typename std::conditional<std::is_enum<T>::value
                , json::int_t
                , typename std::conditional<std::is_integral<T>::value
                    , json::int_t
                    , typename std::conditional<std::is_floating_point<T>::value
                        , double
                        , T
                    >::type
                >::type
            >::type
        >::type type;
    };

    typedef boost::container::flat_map<std::string, int> object_impl_t;
    struct object : object_impl_t
    {  };

    template <typename V>
    void foo(V&& v)
    {
        typedef typename std::decay<V>::type Tv;
        typedef typename inner_type<Tv>::type Ti;
        std::cout << std::is_same<Tv,Ti>::value <<" "<< typeid(Ti).name() <<" "<< typeid(Tv).name() <<" "<< typeid(json::int_t).name() <<"\n";
    }
}

void test_decay_pointer() {
        typedef typename std::decay<const char*>::type ccp;
        typedef typename std::decay<char*>::type cp;
        std::cout
            << std::is_same<ccp,const char*>::value <<" "<< typeid(ccp).name() <<" "<< typeid(const char*).name()
            <<"\n"
            << std::is_same<cp,char*>::value <<" "<< typeid(cp).name() <<" "<< typeid(char*).name()
            <<"\n";
}

namespace R {
    //1. Exact matches
    //2. Type promotion
    //3. Standard conversions
    //4. Constructors and user-defined conversions
    //

    void f(int){} // f1
    void f(int64_t){} // f2
    void f(double){} // f3
    void test()
    {
        f(short(1)); // f1 - Type promotion
    }

    void foo()
    {
        typedef boost::variant<double,bool,int64_t,std::string>
                       variant_t;
        //variant_t v1 = 1;
        variant_t v2 = true;
        variant_t v3 = 1.0;
        variant_t v4 = "";
    }
}

namespace Ns {
    //using boost::variant;
    //typedef variant<double,bool> variant_t;
    struct variant_t : boost::variant<double,bool> {};
    void foo(variant_t& v)
    {}

    struct X;
    struct Y : std::vector<X> {
    };
}

void test_namespace()
{
    Ns::variant_t v;
    foo(v);

    int64_t y = 1;
    int x = y;
}

void test_iterator()
{
    std::vector<int>::iterator it1{0}, it2{0};
    std::cout <<(it1==it2) <<"\n";
}

void test_typeid()
{
    std::cout << typeid(typename std::enable_if<true>::type).name()
        <<" " << typeid(void).name()
        <<" " << std::is_same<void,typename std::enable_if<true>::type>::value
        <<"\n";
}

int main()
{
    //test_iterator();
    isX::foo(123);
    isX::foo(json::int_t(123));
    isX::foo(bool(1));
    //isX::foo(json::none_t());
    test_decay_pointer();
}

