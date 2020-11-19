#include <usbd.h>
#include <usbd_macro.h>
#include <intrman.h>

#define ACM_CTRL_DTR   0x01
#define ACM_CTRL_RTS   0x02
#define USB_BLOCK_SIZE 4096 //Maximum single USB 1.1 transfer length.

typedef struct
{
	int			device_id;
	int			config_id;
        int 			product;
	int			controll;				//controll endpoint
	int			endpin;	
        int 			endpout;				//stream   endpoint
	int			wMaxPacketSize;
	int			result;					//info extracted from callback
	int			bytes;					//
	void		*arg;					//

	
}ARDUINO_DEVICE;

typedef struct _usb_transfer_callback_data {
	int sema;
	u8 *buffer;	
	unsigned int size;
	unsigned int bytes;

} usb_transfer_callback_data;

/* function declaration */
void rpcMainThread(void *param);
void *rpcCommandHandler(int command, void *Data, int Size);



int perform_control(ARDUINO_DEVICE *ino, int req, int val, int index, int leng, void *dataptr);

void PS2InoCallback(int resultCode, int bytes, void *arg);
void PS2InoTransferInCallback(int resultCode, int bytes, void *arg);
void PS2InoTransferOutCallback(int resultCode, int bytes, void *arg);
int perform_bulk_in(ARDUINO_DEVICE *ino, usb_transfer_callback_data *cb_data);
int perform_bulk_out(ARDUINO_DEVICE *ino, usb_transfer_callback_data *cb_data);
int PS2InoConnect(int devId);
int PS2InoProbe(int devId);
int PS2InoDisconnect(int devId);
void *check_arduino(void *Data);
void *configure_arduino(void *Data);
void *write_data(void *Data,int size);
void *read_data(void *Data,int size);
void PS2InoSetDeviceDefaults(ARDUINO_DEVICE *ino);
void PS2InoSetDeviceConfiguration(ARDUINO_DEVICE *dev, int id);
int PS2InoSelectInterface(ARDUINO_DEVICE *dev, int interface, int altSetting);
void PS2InoInitializeNewDevice(ARDUINO_DEVICE *ino);
void *SysAlloc(u64 size);
int SysFree(void *area);


