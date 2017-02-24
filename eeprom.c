#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include "flash900.h"

int eeprom_decode_data(char *msg,unsigned char *datablock)
{
  debug++; dump_bytes(msg,datablock,2048); debug--;
  return 0;
}

int eeprom_parse_line(char *line,unsigned char *datablock)
{
  int address;
  int b[16];
  if (sscanf(line,"EPR:%x : %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x",
	     &address,
	     &b[0],&b[1],&b[2],&b[3],
	     &b[4],&b[5],&b[6],&b[7],
	     &b[8],&b[9],&b[10],&b[11],
	     &b[12],&b[13],&b[14],&b[15])==17) {
    for(int i=0;i<16;i++) datablock[address+i]=b[i];
  }
  
  return 0;
}

char line[1024];
int line_len=0;

int eeprom_parse_output(int fd,unsigned char *datablock)
{
  char buffer[16384];
  int count=get_radio_reply(fd,buffer,16384,0);
  
  for(int i=0;i<count;i++) {
    if (line_len) {
      if (buffer[i]!='\r')
	{ if (line_len<1000) line[line_len++]=buffer[i]; }
      else {
	line[line_len]=0;
	eeprom_parse_line(line,datablock);
	line_len=0;
      }
    } else {
      if ((buffer[i]=='E')&&(buffer[i+1]=='P')) {
	line[0]='E'; line_len=1;
      }
    }
  }
  if (line_len) eeprom_parse_line(line,datablock);
  
  return 0;
}

int eeprom_program(int argc,char **argv)
{
  if ((argc!=10)&&(argc!=3)) {
    fprintf(stderr,"usage: flash900 eeprom <serial port> [<user data file|-> <protected data file|-> <frequency> <txpower> <airspeed> <primary country 2-letter code> <firmware lock (Y|N)]\n");
    exit(-1);
  }
  
  char *userdatafile=argv[3];
  char *protecteddatafile=argv[4];
  int frequency=atoi(argv[5]);
  int txpower=atoi(argv[6]?argv[6]:"0");
  int airspeed=atoi(argv[7]?argv[7]:"0");
  char *primary_country=argv[8];
  char *lock_firmware=argv[9];

  // Start with blank memory block
  unsigned char datablock[2048];
  memset(datablock,2048-64,' ');
  memset(&datablock[2048-64],64,0);

  FILE *f;

  if (argc==9) {
    // Read data files and assemble 2KB data block to write
    if (strcmp(userdatafile,"-")) {
      f=fopen(userdatafile,"r");
      if (!f) {
	perror("Could not open user data file.\n");
      }
      fread(&datablock[0],1024,1,f);
      fclose(f);
    }
    if (strcmp(protecteddatafile,"-")) {
      f=fopen(protecteddatafile,"r");
      if (!f) {
	perror("Could not open user data file.\n");
      }
      fread(&datablock[1024],1024-64,1,f);
      fclose(f);
    }
    datablock[2048-32+0]=(txpower>>8);
    datablock[2048-32+1]=(txpower&0xff);
    datablock[2048-32+2]=(airspeed>>8);
    datablock[2048-32+3]=(airspeed&0xff);
    datablock[2048-32+4]=(frequency>>8);
    datablock[2048-32+13]=lock_firmware[0];
    datablock[2048-32+14]=primary_country[0];
    datablock[2048-32+15]=primary_country[1];
    
  }
  
  int fd=open(argv[2],O_RDWR);
  if (fd==-1) {
    fprintf(stderr,"Could not open serial port '%s'\n",argv[2]);
    exit(-1);
  }
  if (set_nonblock(fd)) {
    fprintf(stderr,"Could not set serial port '%s' non-blocking\n",argv[2]);
    exit(-1);
  }

  // XXX - Short-circuit detection with ! command test to save time
  {
    setup_serial_port(fd,230400);
    char buffer[1024];
    clear_waiting_bytes(fd);
    write_radio(fd,(unsigned char *)"0!g",3);
    usleep(20000);
    write_radio(fd,(unsigned char *)"0!g",3);
    usleep(20000);
    int count=get_radio_reply(fd,buffer,1024,0);
    if (memmem(buffer,count,"EPRADDR=$0",10)) {
      fprintf(stderr,"Radio is ready.\n");
    } else {
      if (detect_speed(fd)) {
	fprintf(stderr,"Could not detect radio speed and mode. Sorry.\n");
	exit(-1);
      }
      if (atmode)
	if (switch_to_online_mode(fd)) {
	  fprintf(stderr,"Could not switch to online mode.\n");
	  exit(-1);
	}      
    }
  }
    
  if (argc==9) {
    // Write it

    eeprom_decode_data("Datablock for writing",datablock);
    
    // Use <addr>!g!y<data>!w sequence to write each 16 bytes
    char cmd[1024];
    int address;
    for(address=0;address<0x800;address+=0x10) {
      snprintf(cmd,1024,"!y%x!g",address);
      write_radio(fd,(unsigned char *)cmd,strlen(cmd));
      // Now write data bytes, escaping ! as !.
      for(int j=0;j<0x10;j++) {
	if (datablock[address+j]=='!') write_radio(fd,(unsigned char *)"!.",2);
	else write_radio(fd,&datablock[address+j],1);
      }
      write_radio(fd,(unsigned char *)"!W",2);
    }
  }
  
  // Verify it
  // Use <addr>!g, !E commands to read out EEPROM data
  unsigned char verifyblock[2048];

  char cmd[1024];
  int address;
  fprintf(stderr,"Reading data from EEPROM"); fflush(stderr);
  for(address=0;address<0x800;address+=0x80) {
    snprintf(cmd,1024,"%x!g",address);
    write_radio(fd,(unsigned char *)cmd,strlen(cmd));
    usleep(20000);
    snprintf(cmd,1024,"!E");
    write_radio(fd,(unsigned char *)cmd,strlen(cmd));
    usleep(300000);
    eeprom_parse_output(fd,verifyblock);
    fprintf(stderr,"."); fflush(stderr);
  }
  fprintf(stderr,"\n"); fflush(stderr);
    
  eeprom_decode_data("Datablock read from EEPROM",verifyblock);
  
  
  return 0;      
}