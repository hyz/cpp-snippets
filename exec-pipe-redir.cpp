#include <cstdlib>
#include <cstring>
#include <iostream>
#include <boost/array.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/range/iterator_range.hpp>
#include <boost/program_options.hpp>
//#include <boost/filesystem/path.hpp>
//#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/format.hpp>
#include "myerror.h"
#include "log.h"

static int exec_redir(std::string arg0, std::string fp, int v[2])
{
    LOG << "run" << fp;

    int inp[2], outp[2];
    pipe(inp);
    pipe(outp);

    pid_t pid = fork();
    if (pid == 0) // child
    {
        close(inp[0]);
        if (inp[1] != STDOUT_FILENO) {
            dup2(inp[1], STDOUT_FILENO);
            close(inp[1]);
        }
        close(outp[1]);
        if (outp[0] != STDIN_FILENO) {
            dup2(outp[0], STDIN_FILENO);
            close(outp[0]);
        }

        int ec;
        execlp(fp.c_str(), arg0.c_str(), 0);
        ec = errno;
        LOG << fp << arg0;
        if (ec == EACCES) {
            execl("/bin/sh", arg0.c_str(), fp.c_str(), 0);
            ec = errno;
            LOG << "/bin/sh" << arg0 << fp;
        }
        LOG << fp << arg0 << "exec fail" << ec;
        kill(getppid(), SIGUSR1);
        exit(ec);
    }

    close(inp[1]);
    close(outp[0]);
    v[0] = inp[0];
    v[1] = outp[1];
    return pid;
}

int main(int argc, char* const argv[])
{
    int v[2] = {0,0};
    int pid = exec_redir("foo", "cat", v);
    if (pid < 0) {
        LOG << "exec_redir fail" << pid;
        return 1;
    }

    dup2(v[0], STDIN_FILENO);
    dup2(v[1], STDOUT_FILENO);

    std::cout << argv[0] <<"\t"<< argv[argc>1?1:0] << "\n";

    std::string line;
    getline(std::cin, line);

    std::cerr << line << "\tOK\n";

    return 0;
}

