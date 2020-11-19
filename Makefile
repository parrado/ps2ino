EE_BIN = main.elf
EE_BIN_PACKED = main-packed.ELF
EE_BIN_STRIPPED = main-stripped.ELF
EE_OBJS = main.o  arduino_rpc.o timer.o
EE_LIBS = -ldebug -lc -lpatches 

all:
	$(MAKE) $(EE_BIN_PACKED) arduino.irx usbd.irx

arduino.irx:
	$(MAKE) -C Arduino

usbd.irx:
	cp $(PS2SDK)/iop/irx/usbd.irx $@
	

clean:
	rm -f *.elf *.ELF *.irx *.o *.s
	$(MAKE) -C Arduino clean		

$(EE_BIN_STRIPPED): $(EE_BIN)
	$(EE_STRIP) -o $@ $<
	
$(EE_BIN_PACKED): $(EE_BIN_STRIPPED)
	ps2-packer -v $< $@
	
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal

