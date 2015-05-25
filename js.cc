#include <stdexcept>
#include <string>
#include <algorithm>
#include <iostream>

struct jsnode
{
    enum { Ty_Str=1, Ty_Int, Ty_Bool };

    virtual jsnode* New(const std::string& k, const char* beg, const char* end) { return 0; }
    virtual void Fin(const std::string& k, jsnode* nd) {}

    virtual jsnode* New(int k, const char* beg, const char* end) { return 0; }
    virtual void Fin(int k, jsnode* nd) {}

    virtual void Set(const std::string& k, const char* beg, const char* end, int ty) {}
    virtual void Set(int k, const char* beg, const char* end, int ty) {}

    virtual ~jsnode() {}
};

struct jsax_key_t;

const int jsax_parse(jsnode* nd, const char* beg, const char* end, const char **pos);

const char* jsax_walk(jsnode* pnd, const char* beg, const char* end, const jsax_key_t& xk);
const char* jsax_array(jsnode* nd, const char* beg, const char* end);
const char* jsax_string(const char* beg, const char* end, const char* ret[2]);
const char* jsax_digital(const char* beg, const char* end, const char* ret[2]);
const char* jsax_boolean(const char* beg, const char* end, const char* ret[2]);

jsnode* jsax_new(jsnode *pnd, const jsax_key_t& xk, const char* beg, const char* end);
void jsax_done(jsnode *pnd, const jsax_key_t& xk, jsnode *nd);
void jsax_set(jsnode *pnd, const jsax_key_t& xk, const char* v[2], int ty);

///////////////
//
//

struct jsax_nullnode : jsnode {
    virtual jsnode* New(const std::string& k, const char* beg, const char* end) { return this; }
    virtual void Fin(const std::string& k, jsnode* nd) {}

    virtual jsnode* New(int k, const char* beg, const char* end) { return this; }
    virtual void Fin(int k, jsnode* nd) {}

    virtual void Set(const std::string& k, const char* beg, const char* end, int ty) {}
    virtual void Set(int k, const char* beg, const char* end, int ty) {}
};

struct jsax_key_t {
    union {
        const char *s[2];
        int i;
    } u;
    int ty;

    jsax_key_t(int y=0) { u.s[0]=u.s[1]=0; ty = y; }
};

struct jsax_fail : std::invalid_argument {
    int errno;
    const char *pos;

    jsax_fail(int ec = 1, const char *p = 0, const char *e = 0)
        : std::invalid_argument(std::string(p,e)) {
            pos = p;
            errno = ec;
        }
};

// #define JSAX_FAIL(p) do{err_ = 1; return p;}while(0)
#define JSAX_FAIL(p,e) do{ throw jsax_fail(1,p,e); }while(0)

const int jsax_parse(jsnode* nd, const char* beg, const char* end, const char **pos)
{
    int ec = 0;

    try {
        beg = jsax_array(nd, beg, end);
    } catch (const jsax_fail& e) {
        beg = e.pos;
        ec = e.errno;
    } catch (...) {
        ec = -1;
    }

    if (pos)
        *pos = beg;
    return ec;
}

static const char* skipss(const char* beg, const char* end)
{
    for (; beg != end; ++beg)
        if (!std::isspace(*beg))
            break;
    return beg;
}

const char *jsax_array(jsnode* nd, const char* beg, const char* end)
{
    const char* const sbeg = beg = skipss(beg, end);

    if (*beg != '{' && *beg != '[')
        JSAX_FAIL(sbeg,end);

    const char* p = skipss(beg+1, end);
    if (p == end)
        JSAX_FAIL(sbeg,end);
    if (*p == *sbeg+2)
        return p + 1;

    jsax_key_t xk;
    xk.ty = *sbeg;
    do {
        ++beg;

        if (xk.ty == '[')
        {
            xk.u.i++;
        }
        else
        {
            beg = jsax_string(beg, end, xk.u.s);
            beg = skipss(beg, end);
            if (beg == end || *beg != ':')
                JSAX_FAIL(beg,end);
            ++beg;
        }
        beg = jsax_walk(nd, beg, end, xk);

        beg = skipss(beg, end);
    } while (*beg == ',');

    if (*beg != *sbeg+2)
        JSAX_FAIL(beg,end);

    return beg + 1;
}

const char *jsax_walk(jsnode* pnd, const char* beg, const char* end, const jsax_key_t& xk)
{
    beg = skipss(beg, end);
    if (beg < end)
    {
        static jsax_nullnode jc_;
        jsnode* jc;

        int ty = 0;
        const char *vals[2];
        switch (*beg)
        {
            case '{': case '[':
                if ( (jc = jsax_new(pnd, xk, beg, end)) == 0)
                    jc = &jc_;
                beg = jsax_array(jc, beg, end);
                jsax_done(pnd, xk, jc);
                return beg;

            case '"': case '\'':
                beg = jsax_string(beg, end, vals);
                ty = jsnode::Ty_Str;
                break;

            case '+': case '-':
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
                beg = jsax_digital(beg, end, vals);
                ty = jsnode::Ty_Int;
                break;

            case 'T': case 'F': case 't': case 'f':
                beg = jsax_boolean(beg, end, vals);
                ty = jsnode::Ty_Bool;
                break;

            default:
                JSAX_FAIL(beg,end);
        }

        jsax_set(pnd, xk, vals, ty);
    }

    return beg;
}

const char* jsax_string(const char* beg, const char* end, const char* ret[2])
{
    const char* const sbeg = beg = skipss(beg, end);
    if ((*beg != '"' && *beg != '\'') || beg+1 >= end)
        JSAX_FAIL(sbeg,end);
    beg = std::find(beg+1, end, *sbeg);
    if (beg == end)
        JSAX_FAIL(sbeg,end);
    ret[0] = sbeg+1;
    ret[1] = beg;
    return beg+1;
}

const char* jsax_digital(const char* beg, const char* end, const char* ret[2])
{
    const char* const sbeg = beg = skipss(beg, end);
    if (*sbeg == '-' || *sbeg == '+')
        ++beg;
    if (beg == end || !std::isdigit(*beg))
        JSAX_FAIL(sbeg,end);
    for (++beg; beg < end; ++beg)
        if (!std::isdigit(*beg) && *beg != '.')
            break;
    if (std::count(sbeg, beg, '.') > 1)
        JSAX_FAIL(sbeg,end);
    ret[0] = sbeg;
    ret[1] = beg;
    return beg;
}

const char* jsax_boolean(const char* beg, const char* end, const char* ret[2])
{
    const char* const sbeg = beg = skipss(beg, end);
    if (*sbeg == 'T' || *sbeg == 't')
    {
        if (end - beg < 4
                || beg[1] != 'r' || beg[2] != 'u' || beg[3] != 'e')
            JSAX_FAIL(beg,end);
        beg += 4;
    }
    else if (*sbeg == 'F' || *sbeg == 'f')
    {
        if (end - beg < 5
                || beg[1] != 'a' || beg[2] != 'l' || beg[3] != 's' || beg[4] != 'e')
            JSAX_FAIL(beg,end);
        beg += 5;
    }
    else
    {
        JSAX_FAIL(beg,end);
    }

    ret[0] = sbeg;
    ret[1] = beg;
    return beg;
}

jsnode* jsax_new(jsnode *pnd, const jsax_key_t& xk, const char* beg, const char* end)
{
    if (xk.ty == '[')
        return pnd->New(xk.u.i, beg, end);
    return pnd->New(std::string(xk.u.s[0], xk.u.s[1]), beg, end);
}
void jsax_done(jsnode *pnd, const jsax_key_t& xk, jsnode *nd)
{
    if (xk.ty == '[')
        pnd->Fin(xk.u.i, nd);
    else
        pnd->Fin(std::string(xk.u.s[0], xk.u.s[1]), nd);
}
void jsax_set(jsnode *pnd, const jsax_key_t& xk, const char* v[2], int ty)
{
    if (xk.ty == '[')
        pnd->Set(xk.u.i, v[0], v[1], ty);
    else
        pnd->Set(std::string(xk.u.s[0], xk.u.s[1]), v[0], v[1], ty);
}

/////////////////////////
//
//
struct P : jsnode
{
    virtual jsnode* New(const std::string& k, const char* beg, const char* end) {
        std::cout << "New " << k << ":" << std::string(beg,end) << "\n";
        return this;
    }
    virtual void Fin(const std::string& k, jsnode* nd) {
        std::cout << "Fin " << k << "\n";
    }

    virtual jsnode* New(int k, const char* beg, const char* end) {
        std::cout << "New " << k << ":" << std::string(beg,end) << "\n";
        return this;
    }
    virtual void Fin(int k, jsnode* nd) {
        std::cout << "Fin " << k << "\n";
    }

    virtual void Set(const std::string& k, const char* beg, const char* end, int ty) {
        std::cout << k << "=" << std::string(beg,end) << "\n";
    }
    virtual void Set(int k, const char* beg, const char* end, int ty) {
        std::cout << k << ":" << std::string(beg,end) << "\n";
    }
};

int main(int ac, char *const av[])
{
    P root;

    std::string line;
    while (std::getline(std::cin, line))
    {
        const char* beg = &line[0];
        const char* end = beg + line.size();
        while (beg < end) {
            const char* sbeg = beg;
            int ec = jsax_parse(&root, beg, end, &beg);
            if (ec)
            {
                break;
            }

            std::cout.write(sbeg, beg - sbeg);
            std::cout << "\n";
        }
    }

    return 0;
}

