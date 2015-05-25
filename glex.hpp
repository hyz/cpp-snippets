
template <typename I_> struct Range {
    I_ begin;
    I_ end;
};

template <typename I_>
static void *_matchres(Range<I_> *cont, Range<I_> *pat)
{
    I_ star;

    if (pat->begin >= pat->end) {
        return cont;
    }

    star = std::find(pat->begin, pat->end, '*');
    if (star == pat->begin) {
        ++pat->begin;
        return _matchres(cont, pat);
    }

    cont->begin = std::search(cont->begin, cont->end, pat->begin, star);

    if (cont->begin >= cont->end) {
        return 0;
    }

    cont->begin += (star - (pat->begin));

    pat->begin = star + 1;

    return _matchres(cont, pat);
}

// static void *_matchres_r(struct wrange *cont, struct wrange *pat)
// {
//     TCHAR *star, *contend;
// 
//     if (pat->end <= pat->begin) {
//         return cont;
//     }
// 
//     star = Wstr_rfind_chr(pat->begin, pat->end, '*');
//     if (star == pat->end - 1) {
//         --pat->end;
//         return _matchres_r(cont, pat);
//     }
//     if (star >= pat->end) {
//         star = pat->begin - 1;
//     }
// 
//     contend = Wstr_rfind(cont->begin, cont->end, star + 1, pat->end);
// 
//     if (contend >= cont->end) {
//         return 0;
//     }
// 
//     cont->end = contend; // cont->end -= pat->end - (star + 1);
// 
//     pat->end = star;
// 
//     return _matchres_r(cont, pat);
// }

template <typename I_>
static void *psearch(Range<I_> *ret, Range<I_> *_pat, Range<I_> *_cont)
{
    Range<I_> pat, cont;
    I_ contbeg, star;

    pat = *_pat;
    cont = *_cont;
    contbeg = cont.begin;

    star = pat.begin;
    if (*star != '*') {
        star = std::find(pat.begin, pat.end, '*');
        if (star >= pat.end) {
            cont.begin = std::search(cont.begin, cont.end, pat.begin, pat.end);
            if (cont.begin >= cont.end)
                return 0;
            ret->begin = cont.begin;
            ret->end = cont.begin + (pat.end - pat.begin);
            return ret;
        }

        contbeg = std::search(cont.begin, cont.end, pat.begin, star);
        if (contbeg >= cont.end) {
            return 0;
        }
        cont.begin = contbeg + (star - pat.begin);
    }

    pat.begin = star + 1;

    if (!_matchres(&cont, &pat))
        return 0;

    ret->begin = contbeg;
    ret->end = cont.begin;

    return ret;
}

// static void *psearch_r(struct wrange *ret, struct wrange *_pat, struct wrange *_cont)
// {
//     struct wrange pat, cont;
//     TCHAR *contend, *star;
// 
//     pat = *_pat;
//     cont = *_cont;
//     contend = cont.end;
// 
//     star = (pat.end - 1);
//     if (*star != '*') {
//         star = Wstr_rfind_chr(pat.begin, pat.end, '*');
//         if (star >= pat.end) {
//             cont.begin = Wstr_rfind(cont.begin, cont.end, pat.begin, pat.end);
//             if (cont.begin >= cont.end)
//                 return 0;
//             ret->begin = cont.begin;
//             ret->end = cont.begin + (pat.end - pat.begin);
//             return ret;
//         }
// 
//         cont.end = Wstr_rfind(cont.begin, cont.end, star + 1, pat.end);
//         if (cont.end >= contend) {
//             return 0;
//         }
// 
//         contend = cont.end + (pat.end - (star + 1));
//     }
// 
//     pat.end = star;
// 
//     if (!_matchres_r(&cont, &pat))
//         return 0;
// 
//     ret->begin = cont.end;
//     ret->end = contend;
// 
//     return ret;
// }

template <typename I_>
void *globex(Range<I_> *ret, Range<I_> *pat, Range<I_> *_cont)
{
    Range<I_> start, stop, rng, res0, res1, cont;
    I_ p; //, *endp;

    if ( (p = std::find(pat->begin, pat->end, '$')) >= pat->end) {
        ret->begin = pat->begin;
        ret->end = pat->end;
        return ret;
    }

    start.begin = pat->begin;
    start.end = p;

    stop.begin = p+1;
    stop.end = pat->end;

    cont = *_cont;

    if (start.begin == start.end) {
        res0.begin = res0.end = cont.begin;
    } else if (!psearch(&res0, &start, &cont)) {
        return 0;
    }

    rng.begin = res0.end;
    rng.end = cont.end;

    if (stop.begin == stop.end) {
        res1.begin = res1.end = rng.end;
    } else if (!psearch(&res1, &stop, &rng)) {
        return 0;
    }

    if (start.begin == start.end) {
        ret->begin = res0.end;
        ret->end = res1.begin;
        return ret;
    }

    typedef std::reverse_iterator<I_> RI_;
    Range<RI_> rrng, rs, rres0;

    rrng.begin = RI_(res1.begin);
    rrng.end = RI_(res0.begin);

    rs.begin = RI_(start.end);
    rs.end = RI_(start.begin);

    if (!psearch(&rres0, &rs, &rrng)) { // if (!psearch_r(&res0, &start, &cont))
        printf("psearch false\n");
        assert(0);
    }

    ret->begin = rres0.begin.base(); // + 1;
    ret->end = res1.begin;

    return ret;
}

template <typename I_>
static std::string glex(const std::string& ex, I_ beg, I_ end)
{
    Range<std::string::const_iterator> _cont, _pat, _ret;

    _cont.begin = beg;
    _cont.end = end;
    _pat.begin = ex.begin();
    _pat.end = ex.end();

    if (!globex(&_ret, &_pat, &_cont)) {
        throw std::logic_error("Ex null");
    }

    return std::string(_ret.begin, _ret.end);
}

template <typename M>
std::string subst(const std::string& cont, const M& vars) // std::string subst(const std::string& cont, const std::map<std::string, std::string>& vars)
{
    std::string ret;
    std::string::const_iterator h, p, q;

    std::string dollar2 = "$$";

    h = cont.begin();
    while ( (p = std::search(h, cont.end(), dollar2.begin(), dollar2.end())) < cont.end()) {
        printf("REPL: ");

        if ( (q = std::search(p+2, cont.end(), dollar2.begin(), dollar2.end())) >= cont.end()) {
            throw std::logic_error("REPL-1");
        }

        std::string k(p+2, q);
        printf(k.c_str());

        std::map<std::string, std::string>::const_iterator iter = vars.find(k);
        if (iter == vars.end()) {
            throw std::logic_error("REPL-2");
        }

        ret.insert(ret.end(), h, p);
        ret += iter->second;

        printf(" {%s}\n", iter->second.c_str());

        h = q+2;
    }
    ret.insert(ret.end(), h, cont.end());

    return ret;
}

