COPT=	-g -Wall

all:	flash900

flash900:	main.c ihex_parse.c ihex_copy.c ihex_record.c speed_detect.c config.h cintelhex.h sha3.c sha3.h eeprom.c flash900.h miniz.c Makefile
	$(CC) $(COPT) -o flash900 main.c ihex_parse.c ihex_copy.c ihex_record.c speed_detect.c sha3.c eeprom.c $(LOPT)
