/*      
  _____     ___ ____ 
   ____|   |    ____|      PS2 Open Source Project
  |     ___|   |____       
  
---------------------------------------------------------------------------

    Copyright (C) 2008 - Neme & jimmikaelkael (www.psx-scene.com) 

    This program is free software; you can redistribute it and/or modify
    it under the terms of the Free McBoot License.
    
	This program and any related documentation is provided "as is"
	WITHOUT ANY WARRANTIES, either express or implied, including, but not
 	limited to, implied warranties of fitness for a particular purpose. The
 	entire risk arising out of use or performance of the software remains
 	with you.
   	In no event shall the author be liable for any damages whatsoever
 	(including, without limitation, damages to your hardware or equipment,
 	environmental damage, loss of health, or any kind of pecuniary loss)
 	arising out of the use of or inability to use this software or
 	documentation, even if the author has been advised of the possibility of
 	such damages.

    You should have received a copy of the Free McBoot License along with
    this program; if not, please report at psx-scene :
    http://psx-scene.com/forums/freevast/

---------------------------------------------------------------------------
*/

#include <tamtypes.h>
#include <kernel.h>
#include <string.h>
#include <sifrpc.h>

// External functions
int arduino_rpc_Init(void);
int check_arduino(void);

#define ARDUINO_IRX 0xA5A5A5A

static SifRpcClientData_t arduino __attribute__((aligned(64)));
static int Rpc_Buffer[1024] __attribute__((aligned(64)));

int arduino_Inited = 0;

typedef struct
{
	u32 ret;
} Rpc_Packet_Send_Check_Arduino;

//--------------------------------------------------------------
int arduinoBindRpc(void)
{
	int ret;
	int retryCount = 0x1000;

	while (retryCount--)
	{
		ret = SifBindRpc(&arduino, ARDUINO_IRX, 0);
		if (ret < 0)
			return -1;
		if (arduino.server != 0)
			break;
		// short delay
		ret = 0x10000;
		while (ret--)
			asm("nop\nnop\nnop\nnop");
	}
	arduino_Inited = 1;
	return retryCount;
}
//--------------------------------------------------------------
int arduino_rpc_Init(void)
{
	arduinoBindRpc();
	if (!arduino_Inited)
		return -1;
	return 1;
}
//--------------------------------------------------------------

//Low level function for driver debugging
int check_arduino(void)
{
	Rpc_Packet_Send_Check_Arduino *Packet = (Rpc_Packet_Send_Check_Arduino *)Rpc_Buffer;

	if (!arduino_Inited)
		arduino_rpc_Init();
	if ((SifCallRpc(&arduino, 1, 0, (void *)Rpc_Buffer, sizeof(Rpc_Packet_Send_Check_Arduino), (void *)Rpc_Buffer, sizeof(int), 0, 0)) < 0)
		return -1;
	return Packet->ret;
}


//Low level function to configure baud rate
void configure_arduino(u32 baudrate)
{
	Rpc_Buffer[0] = baudrate;
	if (!arduino_Inited)
		arduino_rpc_Init();
	(SifCallRpc(&arduino, 2, 0, (void *)Rpc_Buffer, sizeof(int), (void *)Rpc_Buffer, 0, 0, 0));
}

//Low level RPC function to read bytes
int read_bytes(void *buffer, int size)
{

	Rpc_Buffer[0] = size;
	if (!arduino_Inited)
		arduino_rpc_Init();
	SifCallRpc(&arduino, 3, 0, (void *)Rpc_Buffer, sizeof(int), (void *)Rpc_Buffer, size + sizeof(int), 0, 0);

	memcpy(buffer, (u8 *)Rpc_Buffer + sizeof(int), Rpc_Buffer[0]);

	return Rpc_Buffer[0];
}


//Low level RPC function to write bytes
int write_bytes(void *buffer, int size)
{
	Rpc_Buffer[0] = size;
	memcpy((u8 *)Rpc_Buffer + sizeof(int), buffer, size);

	if (!arduino_Inited)
		arduino_rpc_Init();
	SifCallRpc(&arduino, 4, 0, (void *)Rpc_Buffer, size + sizeof(int), (void *)Rpc_Buffer, sizeof(int), 0, 0);

	return Rpc_Buffer[0];
}

//Function to read bytes from arduino
int readArduino(u8 *buffer, int size)
{

	int total = 0;
	int nr = 0;

	while ((total < size))
	{
		nr = read_bytes(buffer + total, size - total);
		total += nr;
	}
	return total;
}

//Function to read line from arduino
void readLineArduino(u8 *buffer)
{
	char c;
	int i = 0;
	while (1)
	{
		if(read_bytes(&c, 1)){
		buffer[i] = c;
		i++;
		if (c == '\n')
			break;
		}
	}
	buffer[i-2] = 0;
}
//--------------------------------------------------------------
