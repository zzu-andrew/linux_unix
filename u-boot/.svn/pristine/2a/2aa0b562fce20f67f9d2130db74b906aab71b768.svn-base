#include <common.h>
#include <watchdog.h>
#include <command.h>
#include <image.h>
#include <malloc.h>
#include <zlib.h>
#include <bzlib.h>
#include <environment.h>
#include <asm/byteorder.h>



int do_andrew (cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
    int i; 
	printf("helo Andrew!, %d\n", argc);

	for(i = 1; i < argc; i ++)
	{
		printf("argv[%d]= %s\n", i , argv[i]);
	}

	return 0;
	
}


U_BOOT_CMD(
 	andrew,	CFG_MAXARGS,	1,	do_andrew,
 	"hello   - just for test for fun!\n",
 	"hello \n\n\n"
 	


);


















