#include <stdlib.h>
#include <stdexcept>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>

using std::find;

struct jsrange
{
    const char *begin;
    const char *end;

    jsrange(const char* b=0, const char *e=0) { begin = b; end = e; }

    inline bool operator==(const jsrange& l)
        { return (begin == l.begin && end == l.end); }
};

struct jserror : std::invalid_argument
{
    const char *begin;
    const char *end;

    jserror(const jsrange& r)
        : std::invalid_argument(std::string(r.begin,r.end))
    { begin = r.begin; end = r.end; }

    static void raise(const jsrange& r) { throw jserror(r); }
};


inline bool empty(jsrange& r)
{
    return r.begin == r.end;
}

inline char front(jsrange& r)
{
    if (empty(r))
        jserror::raise(r);
    return *r.begin;
}

inline char back(jsrange& r)
{
    if (empty(r))
        jserror::raise(r);
    return *(r.end-1);
}

// struct jsexspaces : jsrange {}; jsinput& operator>>(jsinput& jin, jsexspaces& sps);

struct jsinput : jsrange
{
    jsinput(const jsrange& r) : jsrange(r.begin, r.end) {}
    jsinput() {}
};

struct jsnumber : jsrange
{ };

struct jsbool : jsrange
{ };

struct jsstring : jsrange
{ };

struct jsany : jsrange
{ };

// template <typename T> struct jsexarray : jsrange {};
// template <typename T> struct jsexdict : jsrange {};

jsinput& operator>>(jsinput& jin, const jsany& nul);

struct jsstruct
{
    virtual void keyv(jsinput& jin, const std::string& k) = 0;
    virtual ~jsstruct() {}
};

jsinput& operator>>(jsinput& jin, jsstruct& a);

struct NotSpace { bool operator()(int c) const { return !isspace(c); } };
struct IsSpace { bool operator()(int c) const { return isspace(c); } };

////////////////////
//
//
//

//jsinput& operator>>(jsinput& jin, jsexspaces& sps)
//{
//    sps.begin = find(jin.begin, jin.end, IsSpace());
//    sps.end = find(sps.begin, jin.end, NotSpace());
//    return jin;
//}

jsinput& jsex(jsinput& jin, jsstring& ret)
{
    jin.begin = find(jin.begin, jin.end, NotSpace());

    ret = jin;
    switch (front(ret)) {
        case '"': case '\'':
            break;
        default: jserror::raise(jin);
    }

    ++jin.begin;
    do {
        if (front(jin) == front(ret))
            break;
    } while (++jin.begin < jin.end);

    if (empty(jin))
        jserror::raise(jin);
    ret.end = ++jin.begin;

    return jin;
}

inline jsinput& operator>>(jsinput& jin, std::string& ret)
{
    jsstring s;
    jsex(jin, s);
    ret.assign(s.begin+1, s.end-1);
    return jin;
}

jsinput& jsex(jsinput& jin, jsnumber& ret)
{
    const char* sign = 0;
    const char* d = 0;
    const char* dot = 0;

    jin.begin = ret.begin = find(jin.begin, jin.end, NotSpace());

Nxt_Char:
    switch (front(jin))
    {
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
            if (!d)
                d = jin.begin;
            ++jin.begin;
            goto Nxt_Char;
        case '-': case '+':
            if (sign || d)
                jserror::raise(jin);
            sign = jin.begin++;
            goto Nxt_Char;
        case '.':
            if (dot)
                jserror::raise(jin);
            dot = jin.begin++;
            goto Nxt_Char;
        default:
            break;
    }
    if (!d || dot+1 == jin.begin)
        jserror::raise(jin);
    ret.end = jin.begin;

    return jin;
}

template <typename T> jsinput& jsex_xint(jsinput& jin, T& x)
{
    jsnumber numb;
    jsex(jin, numb);
    std::istringstream ins(numb.begin, numb.end);
    ins >> x;
    return jin;
}

inline jsinput& operator>>(jsinput& jin, int& i) { return jsex_xint(jin, i); }
inline jsinput& operator>>(jsinput& jin, unsigned int& i) { return jsex_xint(jin, i); }
inline jsinput& operator>>(jsinput& jin, short& i) { return jsex_xint(jin, i); }
inline jsinput& operator>>(jsinput& jin, unsigned short& i) { return jsex_xint(jin, i); }
inline jsinput& operator>>(jsinput& jin, long& i) { return jsex_xint(jin, i); }
inline jsinput& operator>>(jsinput& jin, unsigned long& i) { return jsex_xint(jin, i); }
inline jsinput& operator>>(jsinput& jin, unsigned char& i) { return jsex_xint(jin, i); }

//inline jsinput& operator>>(jsinput& jin, char& c)
//{
//    jsexspaces sp;
//    return jin >> sp >> c;
//}

inline bool is_starts(const char* p, const char* end, const char* s)
{
    while (*s && p < end && *p == *s)
        { ++p; ++s; }
    return (*s==0);
}

jsinput& jsex(jsinput& jin, jsbool& ret)
{
    jin.begin = ret.begin = find(jin.begin, jin.end, NotSpace());
    switch (front(jin))
    {
        case 't': case 'T':
            if (!is_starts(jin.begin+1, jin.end, "rue"))
                jserror::raise(jin);
            jin.begin += 4;
            break;
        case 'f': case 'F':
            if (!is_starts(jin.begin+1, jin.end, "alse"))
                jserror::raise(jin);
            jin.begin += 5;
            break;
        default:
            jserror::raise(jin);
    }
    ret.end = jin.begin;

    return jin;
}

jsinput& operator>>(jsinput& jin, bool& ret)
{
    jsbool b;
    jsex(jin, b);
    ret = (b.end - b.begin == 4);
    return jin;
}

template <>
struct jsvector : jsarray_base
{
    virtual void element(jsinput& jin) { jsany a; jin >> a; }
};

template <typename C>
struct jsvector : jsarray_base
{
    C *vec_;
    jsvector() { vec_ = 0; }
    jsvector(C& a) { vec_ = &a; }

    virtual void element(jsinput& jin)
    {
        if (vec_) {
            vec_->resize(vec_->size() + 1);
            jin >> vec_->back();
        } else {
            jsany a;
            jin >> a;
        }
    }
};

jsinput& jsex(jsinput& jin, jsarray_base& ret)
{
    jin.begin = ret.begin = find(jin.begin, jin.end, NotSpace());

    if (front(jin) != '[')
        jserror::raise(jin);

    ret.end = find(jin.begin + 1, jin.end, NotSpace());
    if (ret.end < jin.end)
        ++ret.end;
    if (back(ret) == ']')
    {
        jin.begin = ret.end;
        return jin;
    }

    do {
        ++jin.begin;
        jin = ret.element(jin);
        jin.begin = find(jin.begin, jin.end, NotSpace());
    } while (front(jin) == ',');

    if (front(jin) != ']')
        jserror::raise(jin);
    ret.end = ++jin.begin;

    return jin;
}

template <typename T>
jsinput& operator>>(jsinput& jin, std::vector<T>& v)
{
    jsvector<std::vector<T> > vec(v);
    return jsex(jin, vec);
}

struct jsstruct_range : jsrange
{
    jsstruct *node;
    jsstruct_range(jsstruct *a) { node = a; }
};

jsinput& jsex(jsinput& jin, jsstruct_range& ret)
{
    jin.begin = ret.begin = find(jin.begin, jin.end, NotSpace());

    if (front(jin) != '{')
        jserror::raise(jin);

    ret.end = find(jin.begin + 1, jin.end, NotSpace());
    if (ret.end < jin.end)
        ++ret.end();
    if (back(ret) == '}')
    {
        jin.begin = ret.end;
        return jin;
    }

    do {
        jsstring k;
        jsex(jin, k);

        // jsspaces sp; char chr; jin >> sp >> chr >> sp;
        jin.begin = find(jin.begin, jin.end, NotSpace());
        if (front(jin) != ':')
            jserror::raise(jin);
        jin.begin = find(jin.begin+1, jin.end, NotSpace());

        jsrange v = jin;
        if (ret.node)
            ret.node->keyv(jin, std::string(k.begin, k.end));
        if (v == jin)
        {
            jsany a;
            jsex(jin, a);
        }

        jin.begin = find(jin.begin, jin.end, NotSpace());
    } while (front(jin) == ',');

    if (front(jin) != '}')
        jserror::raise(jin);
    ret.end = ++jin.begin;

    return jin;
}

jsinput& operator>>(jsinput& jin, jsstruct& a)
{
    jsstruct_range jn(&a);
    return jsex(jin, jn);
}

jsinput& operator>>(jsinput& jin, jsany& nul)
{
    jin.begin = nul.begin = find(jin.begin, jin.end, NotSpace());
    switch (front(jin))
    {
        case 't': case 'T': case 'f': case 'F':
            { jsbool b; jsex(jin, b); }
            break;
        case '"': case '\'':
            { jsstring x; jsex(jin, x); }
            break;
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
        case '-': case '+':
            { jsnumber x; jsex(jin, x); }
            break;
        case '[':
            { jsvector<> a; jsex(jin, a); }
            break;
        case '{':
            { jsstruct_range a(0); jsex(jin, a); }
            break;
        default:
            jserror::raise(jin);
    }
    nul.end = jin.begin;

    return jin;
}

// { 'int': 1, "str": "s", 'foo':"foo", "vec" :[1,2,3], "b" : { "int" : 9, 'str': '""'}, "": False }
struct A : jsstruct
{
    short aint;
    std::string astr;
    std::vector<int> vec;

    struct B : jsstruct
    {
        short bint;
        std::string bstr;

        virtual void keyv(jsinput& jin, const std::string& k)
        {
            if (k == "int")
                jin >> bint;
            else if (k == "str")
                jin >> bstr;
        }
        void print()
        {
            std::cout << "bint=" << bint << "\n"
                << "bstr=" << bstr << "\n"
                ;
        }
    } b;

    void print()
    {
        std::cout << "aint=" << aint << "\n"
            << "astr=" << astr << "\n"
            << "vec=[" << vec[0] << ",...," << vec[vec.size()-1] << "]\n"
            ;
        b.print();
    }

#define JS_ELSEIF_EX(x, y) else if x y
#define JS_ELSEIF(k, memb, jin) else if (k == #memb) { jin >> memb; }
        // JS_ELSEIF(k, aint, jin)
    virtual void keyv(jsinput& jin, const std::string& k)
    {
        if (0) {}
        else if (k == "aint") { jin >> aint; }
        else if (k == "astr") { jin >> astr; }
        else if (k == "b") { jin >> b; }
        else if (k == "vec") { jin >> vec; }
        // else { jin >> jsany(); }
    }
};

int main(int ac, char *const av[])
{
    std::string line;
    while (std::getline(std::cin, line))
    {
        jsinput jin(&line[0], &line[0] + line.size());

        A a;
        jin >> a;
        a.print();

        std::cout.write(jin.begin, jin.end - jin.begin);
        std::cout << "\n";
    }

    return 0;
}

