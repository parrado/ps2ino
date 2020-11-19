#include <iopcontrol.h>
#include <iopheap.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <stdio.h>
#include <unistd.h>
#include <input.h>
#include <time.h>
#include <string.h>
#include <libcdvd.h>
#include <fcntl.h>
#include <sbv_patches.h>
#include "arduino_rpc.h"

void delay(u64 msec)
{
	u64 start;

	TimerInit();

	start = Timer();

	while (Timer() <= (start + msec))
		;

	TimerEnd();
}

//Segments of code comes from uLE and some works of SP193

void ResetIOP()
{
	SifInitRpc(0);
	while (!SifIopReset("", 0))
	{
	};
	while (!SifIopSync())
	{
	};
	SifInitRpc(0);
}

void InitPS2()
{
	init_scr();
	scr_clear();
	ResetIOP();
	SifInitIopHeap();
	SifLoadFileInit();
	fioInit();
	SifInitRpc(0);
	sbv_patch_disable_prefix_check();
	SifLoadModule("rom0:SIO2MAN", 0, NULL);
	SifLoadModule("rom0:MCMAN", 0, 0);
	SifLoadModule("rom0:MCSERV", 0, 0);
	SifLoadModule("rom0:PADMAN", 0, 0);
	SifLoadModule("mc0:USBD.IRX", 0, 0);
	SifLoadModule("mc0:arduino.irx", 0, 0);
}

int main(int argc, char *argv[], char **envp)
{
	int i;
	char buffer[64];

	InitPS2();
	arduino_rpc_Init();

	scr_printf("\n\n\nPS2Ino By Alex Parrado\n\n\n");
	delay(1000);

	configure_arduino(115200);

	delay(5000);
	
	u8 c;

	//Performs 100 readings
	for (i = 0; i < 100; i++)
	{

		//Sends byte to arduino which initiatites transaction
		buffer[0] = 'f';
		write_bytes(buffer, 1);

		//Reads  lines fro arduino and prints
		readLineArduino(buffer);

		scr_printf("%d: Voltage is: %s v\n", i, buffer);

		//1 second delay
		delay(1000);
	}

	return 0; 
}
