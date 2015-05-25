#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#include <string>
#include <vector>
#include <algorithm>
#include <boost/lexical_cast.hpp>
#include <boost/range.hpp>
#include <boost/filesystem.hpp>

#include <boost/format.hpp>
#include <iostream>

#include "log.h"
#include "util.h"

using namespace boost;
using namespace std;
// using boost::asio::ip::tcp;

template <typename Sink, typename T>
void stringv(const Sink& k, T& arg)
{
    k( lexical_cast<string>(arg) );
}

template <typename Sink, typename T, typename... Args>
void stringv(const Sink& k, T& arg, Args... args)
{
    k( lexical_cast<string>(arg) );
    return stringv(k, args...);
}

template <typename... Args>
pid_t spawn(filesystem::path& fsp, Args... args)
{
    vector<string> argv;
    stringv([&argv](const string& s){ argv.push_back(s); }
            , fsp.string(), fsp.filename().string()
            , args...);

    cout << "spawn: " << argv << endl;

    pid_t pid = fork();
    if (pid < 0)
    {
        return pid;
    }

    if (pid == 0)
    {
        vector<char*> av;
        for_each(argv.begin(), argv.end(),
                [&av](string& a){ av.push_back(const_cast<char*>(a.c_str())); });
        av.push_back(0);

        execv(av[0], &av[1]);

        // BOOST_ASSERT(false);
        exit(123);
    }

    return pid;
}

template <typename Fn>
void walk(const filesystem::path& path, const Fn& fn)
{
    if (filesystem::is_directory(path))
    {
        for_each(filesystem::directory_iterator(path), filesystem::directory_iterator(),
                [&fn](boost::filesystem::directory_entry& it) {
                    walk(it.path(), fn);
                });
    }
    else if (filesystem::is_regular(path)) // filesystem::is_symlink(path) 
    {
        // cout << path << endl;
        fn(path);
    }
    else
    {
        cout << "Unknown " << path << endl;
    }
}

template <typename T>
std::ostream& operator<<(std::ostream& out, const vector<T>& v)
{
    for_each(v.begin(), v.end(), [](const T& t){ cout << t << " "; } );
    return out; // << p.id() << ":" << p.path();
}

template <typename R, typename... Args>
void notify(const R & r, Args... args)
{
    string msg;
    stringv([&msg](const string& s){ msg += " " + s; }, args...);

    cout << "notify:" << r << ":" << msg << endl;

    typedef typename range_iterator<R>::type iterator;
    for (iterator i = std::begin(r); i != std::end(r); ++i)
    {
        sendsms(*i, msg);
    }
}

struct proc
{
    filesystem::path path_;
    pid_t pid_;

    explicit proc(const filesystem::path& p) : path_(p) { pid_ = 0; }
    proc() { pid_ = 0; }

    pid_t start();
    void  stop();

    pid_t id() const { return pid_; }
    const filesystem::path& path() const { return path_; }
};

inline std::ostream& operator<<(std::ostream& out, const proc& p)
{
    return out << p.id() << ":" << p.path();
}

pid_t proc::start()
{
    pid_t pid = spawn(path_, "start"); //(path_, "start")
    if (pid > 0)
    {
        pid_ = pid;
        cout << *this << " started" << endl;
    }
    return pid;
}

void proc::stop()
{
    if (pid_ > 0)
    {
        cout << *this << " stopped" << endl;
        // spawn(path_, "stop");

        kill(pid_, SIGTERM);
        kill(pid_, SIGKILL);
        pid_ = 0;
    }
}

void check_procs(const filesystem::path& path, std::vector<proc>& procs)
{
    std::vector<proc> oldps(std::move(procs)); // oldps.swap(procs);
    BOOST_ASSERT(empty(procs));

    walk(path, [&oldps,&procs](const filesystem::path& path) {
            auto i = find_if(oldps.begin(), oldps.end()
                    , [&path](proc& p) { return p.path() == path; });
            if (i == oldps.end()) {
                i = procs.insert(procs.end(), proc(path));
                i->start();
            } else {
                cout << *i << endl;
                procs.insert(procs.end(), *i);
                oldps.erase(i);
            }
        });

    for_each(oldps.begin(), oldps.end(), [](proc& p){ p.stop(); } );

    cout << "check " << path << " " << procs << endl;
}

static void sig(int sig) {}

int main(int argc, char* argv[])
{
    if (argc == 1)
    {
        std::cerr << "Usage: a.out [path] [phones ...]" << std::endl;
        exit(1);
    }

    filesystem::path path = filesystem::system_complete(argv[1]);
    if (!filesystem::exists(path))
        exit(2);
    cout << "The path: " << argv[1] << " " << path << endl;

    // daemon(0, 0);
    signal(SIGHUP, sig);
    signal(SIGCHLD, sig);
    auto phones = make_iterator_range(&argv[2], &argv[argc]);

    std::vector<proc> procs;

    while (filesystem::exists(path))
    {
        check_procs(path, procs);

        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0)
        {
            cout << "waited:" << pid << ":" << status << endl;
            for_each(procs.begin(), procs.end(),
                    [&pid,&phones](proc& p) {
                        if (p.id() == pid) {
                            cout << "crash:" << p << endl;
                            notify( phones, "Warning:", p.path().string() );
                            p.stop();
                            sleep(1);
                            p.start();
                        }
                    });
        }

        sleep(90);
    }

    return 0;
}

