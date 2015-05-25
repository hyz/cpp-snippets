#include <boost/move/move.hpp>
#include "boost/multi_array.hpp"
#include "boost/cstdlib.hpp"
#include <array>
#include <iostream>

typedef boost::multi_array<char,2> Array2d;
typedef Array2d::array_view<2>::type Array2d_view_t;
typedef std::array<int,2> Point;

template <typename T>
std::ostream& print2d(std::ostream& out, T const& m)
{
    return out;
    for (Point a = {{0,0}}; a[0] != m.size(); ++a[0]) {
        for (a[1] = 0; a[1] != m[0].size()-1; ++a[1])
            out << int( m(a) ) <<" ";
        out << int( m(a) ) <<"\n";
    }
    return out <<"\n";
}

struct V2d : std::array<int,2>
{
    template <typename V> V2d(V const& a) : std::array<int,2>{{a[0], a[1]}} {}
    friend std::ostream& operator<<(std::ostream& out, V2d const& a) {
        return out <<"<"<< a[0]<<","<<a[1] <<">";
    }
};

template <typename M>
inline std::array<int,2> get_shape(M const& m)
{
    return std::array<int,2>{{ int(m.shape()[0]), int(m.shape()[1])  }};
}

template <typename M>
void rotate90_right(M& m)
{
    auto s = get_shape(m);
    BOOST_ASSERT(s[0] == s[1]);
    if (s[0] <= 1)
        return;

    {
        Point p4[4] = {
              {{      0, 0      }}
            , {{      0, s[1]-1 }}
            , {{ s[0]-1, s[1]-1 }}
            , {{ s[0]-1, 0      }}
        };
        for (Array2d::index i = 0; i != m.size()-1; ++i) {
            for (int x=1; x < 4; ++x)
                std::swap(m(p4[0]), m(p4[x]));
            p4[0][1]++; p4[1][0]++; p4[2][1]--; p4[3][0]--;
        }
    }

    if (s[0] == 2)
        return;

    typedef Array2d::index_range range;
    Array2d::array_view<2>::type n = m[boost::indices
            [range(1, s[0]-1)]
            [range(1, s[1]-1)]
        ];
    return rotate90_right(n);
}

inline Point operator-(Point const& rhs, Point const& lhs)
{
    return Point{{rhs[0]-lhs[0], rhs[1]-lhs[1]}};
}
void break_p() {}

struct Array2dx : Array2d
{
    char dummy_;
    char& at(Point const& a)
    {
        if (a[1] < 0 || a[1] >= int(shape()[1]) || a[0] >= int(shape()[0])) {
            // std::cerr << V2d(a) <<" 1\n"; break_p();
            return (dummy_=1);
        }
        if (a[0] < 0) {
            // std::cerr << V2d(a) <<" 0\n";
            return (dummy_=0);
        }
        return (*this)(a);
    }
    char  at(Point const& a) const { return const_cast<Array2dx&>(*this).at(a); }
};

template <typename N>
bool is_collision(Array2dx& va, Point bp, N const& n)
{
    auto s = get_shape(n);
    Point ep = {{ bp[0]+s[0], bp[1]+s[1] }};

    for (auto p=bp; p[0] != ep[0]; ++p[0]) {
        for (p[1] = bp[1]; p[1] != ep[1]; ++p[1]) {
            if (n(p - bp) && va.at(p)) {
                return 1;
            }
        }
    }
    return 0;
}

template <typename N>
Array2dx& or_assign(Array2dx& va, Point bp, N const& n)
{
    auto s = get_shape(n);
    Point ep = {{ bp[0]+s[0], bp[1]+s[1] }};

    for (auto p = bp; p[0] != ep[0]; ++p[0]) {
        for (p[1] = bp[1]; p[1] != ep[1]; ++p[1]) {
            va.at(p) |= n(p - bp);
        }
    }
    return va;
}

struct Main // : Array2d
{
    std::vector<std::pair<char*,size_t>> const const_mats_;
    Array2dx vmat_;
    Point p_;
    Array2d smat_, pv_;
    time_t tb_, td_;

    Main() // (char v[], size_t N, size_t n)
        //: Array2d(boost::extents[N/n][n])
        : const_mats_(mats_init())
    {
        // BOOST_ASSERT(N % n == 0); assign(v, N);
        ::srand(time(0));
    }

    void new_game(size_t h, size_t w)
    {
        //BOOST_ASSERT(h * w == sizeof(cmat_));
        vmat_.resize(boost::extents[h][w]);
        for (int y=0; y < h; ++y)
            for (int x=0; x < w; ++x)
                vmat_[y][x] = 0;
        //vmat_.assign(cmat_, cmat_ + sizeof(cmat_));

        pop_preview(0);

        tb_ = time(0);
    }

    bool next_round()
    {
        pop_preview(&smat_);

        p_[1] = int(vmat_[0].size() - smat_[0].size()) / 2;
        p_[0] = -int(smat_.size()-1);
        while (p_[0] <= 0) {
            Point tmp = p_;
            if (is_collision(vmat_, p_, smat_)) {
                or_assign(vmat_, p_, smat_);
                over();
                return 0;
            }
            if (p_[0] == 0)
                break;
            ++p_[0];
        }
        td_ = time(0);
        return 1;
    }

    bool Move(int di)
    {
        //std::cerr <<p_[0]<<p_[1]<< "\n";
        auto tmp = p_;
        if (di == 0) {
            tmp[0]++;
        } else if (di < 0) {
            tmp[1]--;
        } else {
            tmp[1]++;
        }

        if (is_collision(vmat_, tmp, smat_)) {
            if (di == 0) {
                or_assign(vmat_, p_, smat_);
                auto sv = get_shape(vmat_);
                auto sa = get_shape(smat_);
                collapse(std::min(p_[0]+sa[0], sv[0]-1), std::max(0, p_[0]));
            }
            return false;
        }
        // std::cerr << V2d(tmp) <<" not collis\n";

        p_ = tmp; //std::cerr << V2d(p_) << "\n";
        if (di == 0) {
            td_ = time(0);
        }
        return true;
    }

    bool rotate()
    {
        auto tmp = smat_;
        rotate90_right(tmp);
        if (is_collision(vmat_, p_, tmp)) {
            return false;
        }
        std::swap(smat_,tmp);
        return true;
    }

private:
    void over()
    {
        std::cerr << "over\n";
    }

    void clear(Array2d::index row) {
        for (auto& x : vmat_[row])
            x = 0;
    }
    template <typename R> void move_(Array2d::index src, Array2d::index dst) {
        vmat_[dst] = vmat_[src];
        clear(src);
    }

    void collapse(Array2d::index rb, Array2d::index r0, int nc=0)
    {
        auto const & row = this->vmat_[rb];

        if (rb >= r0) {
            if (std::find(row.begin(), row.end(), 0) == row.end()) {
                clear(rb);
                ++nc;
            } else if (nc > 0) {
                vmat_[rb+nc] = row; clear(rb);
            } else if (rb == r0 && nc == 0) {
                return;
            }
        } else if (nc > 0) {
            // auto pred = [](int x) -> bool { return x!=0; };
            // if (std::find(row.begin(), row.end(), pred) == row.end())
                ;
            vmat_[rb+nc] = row; clear(rb);
        }

        if (rb > 0) {
            collapse(rb-1, r0, nc);
        }
    }

    void pop_preview(Array2d* a)
    {
        if (a) {
            a->resize(boost::extents[pv_.size()][pv_[0].size()]);
            *a = pv_;
            print2d(std::cerr, *a);
        }
        auto p = const_mats_[::rand() % const_mats_.size()];
        int x = ::sqrt(p.second);
        pv_.resize(boost::extents[x][x]);
        pv_.assign(p.first, p.first + p.second);
        print2d(std::cerr, pv_);
    }

    static std::vector<std::pair<char*,size_t>> mats_init()
    {
        static char cmat_O[] = {
            1,1,
            1,1
        };
        static char cmat_T[] = {
            0, 1, 0,
            1, 1, 1,
            0, 0, 0
        };
        static char cmat_Z[] = {
            1, 1, 0,
            0, 1, 1,
            0, 0, 0
        };
        static char cmat_N[] = {
            0, 1, 1,
            1, 1, 0,
            0, 0, 0
        };
        static char cmat_F[] = {
            1, 1, 0,
            0, 1, 0,
            0, 1, 0
        };
        static char cmat_E[] = {
            0, 1, 1,
            0, 1, 0,
            0, 1, 0
        };
        static char cmat_L[] = {
            0, 1, 0, 0,
            0, 1, 0, 0,
            0, 1, 0, 0,
            0, 1, 0, 0
        };

        std::vector<std::pair<char*,size_t>> mats;
        mats.emplace_back(cmat_O, sizeof(cmat_O));
        mats.emplace_back(cmat_T, sizeof(cmat_T));
        mats.emplace_back(cmat_Z, sizeof(cmat_Z));
        mats.emplace_back(cmat_N, sizeof(cmat_N));
        mats.emplace_back(cmat_F, sizeof(cmat_F));
        mats.emplace_back(cmat_E, sizeof(cmat_E));
        mats.emplace_back(cmat_L, sizeof(cmat_L));
        return boost::move(mats);
    }

    friend std::ostream& operator<<(std::ostream& out, Main const& M)
    {
        auto& m = M.vmat_;
        for (Array2d::index i = 0; i != m.size(); ++i)
        {
            for (Array2d::index j = 0; j != m[0].size()-1; ++j)
            {
                if (i >= M.p_[0] && i < M.p_[0]+M.smat_.size()
                        && (j >= M.p_[1] && j < M.p_[1]+M.smat_[0].size())) {
                    std::cerr << int(m[i][j] || M.smat_[i-M.p_[0]][j-M.p_[1]]) <<" ";
                    continue;
                }
                std::cerr << int(m[i][j]) <<" ";
            }
            std::cerr << int(m[i][m[0].size()-1])<<"\n";
        }
        return out;
    }
};

int main(int argc, char* const argv[])
{
    Main M;
    M.new_game(20, 10);  //std::cerr << M << "\n";
    while (M.next_round()) {
        M.rotate(); // std::cerr << M << "\n";
        //while (M.Move(-1)) ; std::cerr << M << "\n";
        //while (M.Move( 1)) ; std::cerr << M << "\n";
        while (M.Move(0)) ; std::cerr << M << "\n";
    }
    //std::cerr << M << "\n";
    return 0;
}


