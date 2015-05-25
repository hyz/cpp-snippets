// #include <stdio.h>

#ifdef MAIN

static int s_int;
extern int e_int;

static void s_func(int *val)
{
    *val = 0x00000001;
}

extern void e_func(int *val);

int main()
{
    s_func(&s_int);
    e_func(&e_int);
    return (s_int + e_int);
}
#else

//////////////
int eg_int;

void e_func(int *val)
{
    *val = 0x00000200;
}

#endif

