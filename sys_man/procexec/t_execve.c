/*************************************************************************\
*                  Copyright (C) Michael Kerrisk, 2018.                   *
*                                                                         *
* This program is free software. You may use, modify, and redistribute it *
* under the terms of the GNU General Public License as published by the   *
* Free Software Foundation, either version 3 or (at your option) any      *
* later version. This program is distributed without any warranty.  See   *
* the file COPYING.gpl-v3 for details.                                    *
\*************************************************************************/

/* Listing 27-1 */

/* t_execve.c

   Demonstrate the use of execve() to execute a program.
*/

//execve 系统调用，可以将新程序加载到某一进程的内存空间，这一操作过程中将丢弃旧有程序，而进程的栈数据以及堆段数=会
//被新程序相应的部件所替换

#include "tlpi_hdr.h"

int
main(int argc, char *argv[])
{
	//和char *argv[]，主函数中传入的参数是一致的，参数传入采用指针数组的方式传入
    char *argVec[10];           /* Larger than required */
    //< 用指针数组的方式传入参数，在PPPoE中的拨号参数也是这样传入的
    char *envVec[] = { "GREET=salut", "BYE=adieu", NULL };

    if (argc != 2 || strcmp(argv[1], "--help") == 0)
        usageErr("%s pathname\n", argv[0]);

    /* Create an argument list for the new program */

    argVec[0] = strrchr(argv[1], '/');      /* Get basename from argv[1] */
    if (argVec[0] != NULL)
        argVec[0]++;
    else
        argVec[0] = argv[1];
    argVec[1] = "hello world";
    argVec[2] = "goodbye";
    argVec[3] = NULL;           /* List must be NULL-terminated */

    /* Execute the program specified in argv[1] */

    execve(argv[1], argVec, envVec);
    errExit("execve");          /* If we get here, something went wrong */
}
