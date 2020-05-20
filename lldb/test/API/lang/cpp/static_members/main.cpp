#include "main.h"

int main()
{
    A my_a;
    my_a.m_a = 1;
    my_a.m_c = 3;

    my_a.access(); // breakpoint 1 
    return 0;
}

