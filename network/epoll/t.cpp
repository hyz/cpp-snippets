#include <stdio.h>
#include "clock.hpp"

int main(int argc, char* const argv[])
{
    printf("microseconds %u\n", microseconds(clock_realtime_type::now() - clock_realtime_type::zero()));
    printf("milliseconds %u\n", milliseconds(clock_realtime_type::now() - clock_realtime_type::zero()));
    return 0;
}

