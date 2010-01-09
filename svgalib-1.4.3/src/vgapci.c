#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "libvga.h"

#define PCI_CONF_ADDR  0xcf8
#define PCI_CONF_DATA  0xcfc

int __svgalib_use_procpci=0;
int __svgalib_pci_ibus=0, __svgalib_pci_idev=0;
static int pci_read_config (unsigned char bus, unsigned char device,
        unsigned char fn, unsigned long *buf, int size)
{
  int i;
  unsigned long bx = ((fn&7)<<8) | ((device&31)<<11) | (bus<<16) |
                        0x80000000;

  for (i=0; i<size; i++) {
        outl (PCI_CONF_ADDR, bx|(i<<2));
        buf[i] = inl (PCI_CONF_DATA);
  }

  return 0;
}

static int proc_pci_read_config(unsigned char bus, unsigned char device,
        unsigned char fn, unsigned long *buf, int size)
{
   int f,i;
   char filename[256];
   
   sprintf(filename,"/proc/bus/pci/%02i/%02x.%i",bus,device,fn);
   f=open(filename,O_RDONLY);
   if(read(f,buf,4*size)<1)for(i=0;i<63;i++)buf[i]=-1;
   close(f);
   return 0;
};

/* find a vga device of the specified vendor, and return
   its configuration (16 dwords) in conf 
   return zero if device found.

   Remark 1: It's the caller's responsibility to make sure that
             the PCI ports are accessible. In practice, (under kernels
             2.0.x, I don't know about 2.1.x) this means iopl(3).  

*/ 

int __svgalib_pci_find_vendor_vga(unsigned int vendor, unsigned long *conf, int cont)
{ unsigned long buf[64];
  int bus,device=0;
  
  cont++;

  for(bus=__svgalib_pci_ibus;(bus<16)&&cont;bus++)              
    for(device=__svgalib_pci_idev;(device<32)&&cont;device++){
      if(__svgalib_use_procpci)proc_pci_read_config(bus,device,0,buf,3); 
          else pci_read_config(bus,device,0,buf,3);
      if(((buf[0]&0xffff)==vendor)&&
        (((buf[2]>>16)&0xffff)==0x0300))  /* VGA Class */
              if(!(--cont)){
                if(__svgalib_use_procpci)proc_pci_read_config(bus,device,0,buf,64); 
                   else pci_read_config(bus,device,0,buf,64);
                memcpy(conf,buf,256); 
                return 0;
              };
    };

  return cont;
}

