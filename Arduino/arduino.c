/*      
  _____     ___ ____ 
   ____|   |    ____|      PS2 Open Source Project
  |     ___|   |____       
  
---------------------------------------------------------------------------
USB Driver for Arduino UNO, By Alex Parrado (2020)
*/

#include <stdio.h>
#include <intrman.h>
#include <loadcore.h>
#include <sifcmd.h>
#include <sysclib.h>
#include <sysmem.h>
#include <thbase.h>
#include <timrman.h>
#include <xtimrman.h>
#include <thsemap.h>
#include "Arduino.h"
#include "fifo.h"

//Vendor and product ID for Arduino UNO
#define ARDUINO_VENDOR 0x2341
#define ARDUINO_UNO_PRODUCT 0x0043

//Size of FIFO
#define FIFO_SIZE 1024

//Module identificaton
#define MODNAME "arduino"
IRX_ID(MODNAME, 1, 1);

#define ARDUINO_IRX 0xA5A5A5A

int __attribute__((unused)) shutdown()
{
	return 0;
}

//Required data for RPC
static SifRpcDataQueue_t Rpc_Queue __attribute__((aligned(64)));
static SifRpcServerData_t Rpc_Server __attribute((aligned(64)));
static int Rpc_Buffer[1024] __attribute((aligned(64)));

//Structure for Arduino Driver
UsbDriver arduino_driver = {NULL,
							NULL,
							"ps2ino",
							PS2InoProbe,
							PS2InoConnect,
							PS2InoDisconnect};

//Arduino device, just one device allowed
ARDUINO_DEVICE arduino;

//Semaphore for synchronization of bulk in transfers
int ps2ino_sema_in = 0;

//Semaphore for synchronization of bulk in transfers
int ps2ino_sema_out = 0;

//Semaphore for control transfers
int ps2ino_sema_control = 0;

//Semaphore for FIFO mutual exclusion
int fifo_sema = 0;

//ID of initialization thread
static int maintain_thread;

//ID of FIFO thread
static int fifo_thread;

//Buffer for FIFO
u8 *buffer;

//Buffer for bulk in transfers
u8 *bulkBuffer;

//The FIFO object itself
FIFO_BUFFER fifo;

//Callback for control transfers
void PS2InoCallback(int resultCode, int bytes, void *arg)
{
	int sema = (int )arg;

	if (resultCode != 0)
		printf("callback: result= %d, bytes= %d, arg= %p \n", resultCode, bytes, arg);

	SignalSema(sema);
}


//Callback for bulk out transfers
void PS2InoOutTransferCallback(int resultCode, int bytes, void *arg)
{
	if (resultCode == USB_RC_OK)

		SignalSema(ps2ino_sema_out);
}

//Callback for bulk in transfers
void PS2InoInTransferCallback(int resultCode, int bytes, void *arg)
{

	int i;

	*(int *)arg = bytes;

	//If bytes were received
	if (resultCode == USB_RC_OK && bytes != 0)
	{

		//Copy bytes to FIFO
		for (i = 0; i < bytes; i++)
		{

			WaitSema(fifo_sema);

			FIFO_Put(&fifo, bulkBuffer[i]);

			SignalSema(fifo_sema);
		}
	}

	SignalSema(ps2ino_sema_in);
}

//--------------------------------------------------------------
/* Module entry point */
int _start(int argc, char **argv)
{
	iop_thread_t param;
	iop_sema_t sema;
	int id;

	//USB transfers semaphores initializaton
	sema.initial = 0;
	sema.max = 1;
	sema.option = 0;
	sema.attr = 0;

	ps2ino_sema_control = CreateSema(&sema);
	ps2ino_sema_in = CreateSema(&sema);
	ps2ino_sema_out = CreateSema(&sema);

	/*RPC thread*/
	param.attr = TH_C;
	param.thread = rpcMainThread;
	param.priority = 40;
	param.stacksize = 0x800;
	param.option = 0;

	if ((id = CreateThread(&param)) <= 0)
		return MODULE_NO_RESIDENT_END;

	StartThread(id, 0);

	//Driver registration
	sceUsbdRegisterLdd(&arduino_driver);

	return MODULE_RESIDENT_END;
}
//Thread for RPC
void rpcMainThread(void *arg)
{
	SifInitRpc(0);
	SifSetRpcQueue(&Rpc_Queue, GetThreadId());
	SifRegisterRpc(&Rpc_Server, ARDUINO_IRX, (void *)rpcCommandHandler, (u8 *)&Rpc_Buffer, 0, 0, &Rpc_Queue);
	SifRpcLoop(&Rpc_Queue);
}

//Function to perform control transfer
int perform_control(ARDUINO_DEVICE *ino, int req, int val, int index, int leng, void *dataptr)
{

	int ret;

	ret = sceUsbdControlTransfer(ino->controll, 0x21, req, val, index, leng, dataptr, PS2InoCallback, (void *)ps2ino_sema_control);

	if (ret != USB_RC_OK)
	{
		printf("Control transfer failed\n");
		ret = -1;
	}
	else
	{
		WaitSema(ps2ino_sema_control);
	}

	return ret;
}


//Thread function for FIFO and bulk in transfers
void fifoThread(void *args)
{

	int ret;
	int nbytes;

	while (true)
	{
		//Asks for up to wMaxPacketSize bytes
		ret = sceUsbdBulkTransfer(arduino.endpin, bulkBuffer, arduino.wMaxPacketSize, PS2InoInTransferCallback, &nbytes);

		if (ret != USB_RC_OK)
		{
			printf("Bulk in failed.\n");
		}
		else
		{

			WaitSema(ps2ino_sema_in);
		}
	}
}
//Configures arduino baud rate
void *configure_arduino(void *Data)
{

	iop_thread_t param;
	iop_sema_t sema;

	u32 *ret = Data;
	u32 baudrate = ret[0];

	//Selects interface 0
	PS2InoSelectInterface(&arduino, 0, 0);

	perform_control(&arduino, 0x22, ACM_CTRL_DTR | ACM_CTRL_RTS, 0, 0, NULL);

	//Converts baudrate from BE to LE
	unsigned char encoding[] = {baudrate & 0xff, (baudrate >> 8) & 0xff, (baudrate >> 16) & 0xff, (baudrate >> 24) & 0xff, 0x00, 0x00, 0x08};

	perform_control(&arduino, 0x20, 0, 0, sizeof(encoding), encoding);

	//Selects interface 1

	PS2InoSelectInterface(&arduino, 1, 0);

	//Room for FIFO buffer and bulk in buffer
	buffer = SysAlloc(FIFO_SIZE);
	bulkBuffer = SysAlloc(arduino.wMaxPacketSize);

	//FIFO initialization
	FIFO_Init(&fifo, buffer, FIFO_SIZE);

	//FIFO semaphore initializaton
	sema.initial = 1;
	sema.max = 1;
	sema.option = 0;
	sema.attr = 0;

	fifo_sema = CreateSema(&sema);

	/*FIFO thread*/
	param.attr = TH_C;
	param.thread = fifoThread;
	param.priority = 50;
	param.stacksize = 0x800;
	param.option = 0;

	fifo_thread = CreateThread(&param);

	StartThread(fifo_thread, 0);

	return Data;
}
//The RPC command handler just 4 commands, one for debug
void *rpcCommandHandler(int command, void *Data, int Size)
{
	int *aux = Data;

	switch (command)
	{
	case 1:
		Data = check_arduino(Data);
		break;
	case 2:
		//To set baud rate
		Data = configure_arduino(Data);
		break;

	case 3:
		//To read data
		Data = read_data(Data, aux[0]);
		break;

	case 4:
		//To write data
		Data = write_data(Data, aux[0]);
		break;
	}

	return Data;
}

//Just for debugging purposes
void *check_arduino(void *Data)
{

	return Data;
}

//Writes data to Arduino
void *write_data(void *Data, int size)
{

	int ret;
	int nbytes;
	u8 *ret1 = (u8 *)Data + sizeof(int);
	int *ret2 = (int *)Data;

	//Performs bulk out transfer
	ret = sceUsbdBulkTransfer(arduino.endpout, ret1, size, PS2InoOutTransferCallback, (void *)&nbytes);

	if (ret != USB_RC_OK)
	{
		printf("Bulk in failed.\n");
	}
	else
	{
		WaitSema(ps2ino_sema_out);
	}

	//Returns number of written bytes
	ret2[0] = nbytes;

	return Data;
}

//Reads data from arduino
void *read_data(void *Data, int size)
{

	u8 *ret1 = (u8 *)Data + sizeof(int);
	int *ret2 = (int *)Data;
	int nbytes = 0;
	bool flag;

	u8 c;

	//Returns if requested bytes were read or FIFO is empty
	while (true)
	{

		if (nbytes == size)
			break;

		//Reads FIFO
		WaitSema(fifo_sema);
		flag = FIFO_Get(&fifo, &c);
		SignalSema(fifo_sema);

		//If FIFO was not empty
		if (flag)
		{
			ret1[nbytes] = c;
			nbytes++;
		}
		else
			break;
	}

	//Return number of read bytes
	ret2[0] = nbytes;

	return Data;
}

//Default baud rate of 9600
void PS2InoSetDeviceDefaults(ARDUINO_DEVICE *ino)
{

	perform_control(ino, 0x22, ACM_CTRL_DTR | ACM_CTRL_RTS, 0, 0, NULL);

	unsigned char encoding[] = {0x80, 0x25, 0x00, 0x00, 0x00, 0x00, 0x08};

	perform_control(ino, 0x20, 0, 0, sizeof(encoding), encoding);
}

/** selects the configuration for the device */
void PS2InoSetDeviceConfiguration(ARDUINO_DEVICE *dev, int id)
{
	int ret;

	ret = sceUsbdSetConfiguration(dev->controll, id, PS2InoCallback, (void *)ps2ino_sema_control);

	if (ret != USB_RC_OK)
	{
		printf("Usb: Error sending set_configuration\n");
	}
	else
	{
		WaitSema(ps2ino_sema_control);
	}
}

/** Selects the interface and alternative settting to use */
int PS2InoSelectInterface(ARDUINO_DEVICE *dev, int interface, int altSetting)
{
	int ret;

	ret = sceUsbdSetInterface(dev->controll, interface, altSetting, PS2InoCallback, (void *)ps2ino_sema_control);

	if (ret != USB_RC_OK)
	{
		printf("Usb: PS2CamSelectInterface  Error...\n");
	}
	else
	{
		WaitSema(ps2ino_sema_control);
	}

	return ret;
}

/** called after an Arduino is accepted */
void PS2InoInitializeNewDevice(ARDUINO_DEVICE *ino)
{

	PS2InoSetDeviceConfiguration(ino, 1);

	PS2InoSelectInterface(ino, 0, 0);

	//PS2InoSetDeviceDefaults(ino);

	PS2InoSelectInterface(ino, 1, 0);

	DeleteThread(maintain_thread);

	return;
}

/*---------------------------------------*/
/*-	checks if the device is pluged in	-*/
/*---------------------------------------*/
int PS2InoProbe(int devId)
{
	//static short			count;

	UsbDeviceDescriptor *dev;
	UsbConfigDescriptor *conf;
	UsbInterfaceDescriptor *intf;

	dev = UsbGetDeviceStaticDescriptor(devId, NULL, USB_DT_DEVICE);
	conf = UsbGetDeviceStaticDescriptor(devId, dev, USB_DT_CONFIG);
	intf = (UsbInterfaceDescriptor *)((char *)conf + conf->bLength);

	if (intf->bInterfaceClass == USB_CLASS_COMM)
	{
		// Arduino uno
		if (dev->idVendor == ARDUINO_VENDOR && dev->idProduct == ARDUINO_UNO_PRODUCT)
		{
			return 1;
		}
	}

	return 0;
}

//when arduino is connected
int PS2InoConnect(int devId)
{
	ARDUINO_DEVICE *ino = NULL;
	iop_thread_t param;

	UsbDeviceDescriptor *dev;
	UsbConfigDescriptor *conf;

	UsbInterfaceDescriptor *intf0, *intf1;
	UsbEndpointDescriptor *endpin, *endpout;

	//printf("Arduino was connected\n");

	dev = UsbGetDeviceStaticDescriptor(devId, NULL, USB_DT_DEVICE);
	conf = UsbGetDeviceStaticDescriptor(devId, dev, USB_DT_CONFIG);

	intf0 = UsbGetDeviceStaticDescriptor(devId, dev, USB_DT_INTERFACE);
	intf1 = UsbGetDeviceStaticDescriptor(devId, intf0, USB_DT_INTERFACE);

	endpout = (UsbEndpointDescriptor *)((char *)intf1 + intf1->bLength);
	endpin = (UsbEndpointDescriptor *)((char *)endpout + endpout->bLength);

	arduino.device_id = devId;
	arduino.product = dev->idProduct;
	arduino.wMaxPacketSize = ((endpin->wMaxPacketSizeHB) << 8) | ((endpin->wMaxPacketSizeLB));
	arduino.controll = UsbOpenEndpoint(devId, NULL);
	arduino.endpin = UsbOpenEndpointAligned(devId, endpin);
	arduino.endpout = UsbOpenEndpointAligned(devId, endpout);

	/*create thread that will execute funtions that cant be called*/
	/*in this funtion*/
	param.attr = TH_C;
	param.thread = (void *)PS2InoInitializeNewDevice;
	param.priority = 40;
	param.stacksize = 0x800;
	param.option = 0;

	maintain_thread = CreateThread(&param);

	ino = &arduino;
	StartThread(maintain_thread, ino);

	return 0;
}

int PS2InoDisconnect(int devId)
{

	//	printf("Arduino was unplugged\n");

	arduino.product = -1;

	// close endpoints
	UsbCloseEndpoint(arduino.controll);
	UsbCloseEndpoint(arduino.endpin);
	UsbCloseEndpoint(arduino.endpout);

	DeleteThread(fifo_thread);

	SysFree(buffer);
	SysFree(bulkBuffer);

	return 0;
}

//--------------------------------------------------------------
void *SysAlloc(u64 size)
{
	int oldstate;
	register void *p;

	CpuSuspendIntr(&oldstate);
	p = AllocSysMemory(ALLOC_FIRST, size, NULL);
	CpuResumeIntr(oldstate);

	return p;
}
//--------------------------------------------------------------
int SysFree(void *area)
{
	int oldstate;
	register int r;

	CpuSuspendIntr(&oldstate);
	r = FreeSysMemory(area);
	CpuResumeIntr(oldstate);

	return r;
}
//--------------------------------------------------------------
