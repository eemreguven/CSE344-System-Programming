#include "gtu_grades.h"

int main()
{
    while (execute_command() != -1)
    {}
    log_msg(NULL, NULL, "System terminated.");
    exit(EXIT_SUCCESS);

    return 0;
}
