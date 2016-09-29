#include <stdio.h>
//#include <unistd.h>
#include <termios.h>
//#include <sys/ioctl.h>
//#include <sys/time.h>
//#include <sys/types.h>
#include <fcntl.h>

//------------------------------------------------------------------------------
// getkey() returns the next char in the stdin buffer if available, otherwise
//          it returns -1 immediately.
//
int getkey(void)
{
    char ch;
    int error;
    struct termios oldAttr, newAttr;
    int oldFlags, newFlags;
    struct timeval tv;
    int fd = fileno(stdin);
    tcgetattr(fd, &oldAttr);
    newAttr = oldAttr;
    oldFlags = fcntl(fd, F_GETFL, 0);

    newAttr.c_iflag = 0; /* input mode */
    newAttr.c_oflag = 0; /* output mode */
    newAttr.c_lflag &= ~ICANON; /* line settings */
    newAttr.c_cc[VMIN] = 1; /* minimum chars to wait for */
    newAttr.c_cc[VTIME] = 1; /* minimum wait time */

    // Set stdin to nonblocking, noncanonical input
    fcntl(fd, F_SETFL, O_NONBLOCK);
    error=tcsetattr(fd, TCSANOW, &newAttr);

    tv.tv_sec = 0;
    tv.tv_usec = 10000; // small 0.01 msec delay
    select(1, NULL, NULL, NULL, &tv);

    if (error == 0)
        error=(read(fd, &ch, 1) != 1); // get char from stdin

    // Restore original settings
    error |= tcsetattr(fd, TCSANOW, &oldAttr);
    fcntl(fd, F_SETFL, oldFlags);

    return (error ? -1 : (int) ch);
}

int main()
{
    int c,n=0;
    printf("Hello, world!\nPress any key to exit. I'll wait for 4 keypresses.\n\n");
    while (n<4)
    {
        //printf("."); // uncomment this to print a dot on each loop iteration
        c = getkey();
        if (c >= 0)
        {
            printf("You pressed '%c'\n", c);
            ++n;
        }
    }
}
