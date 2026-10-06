/* Private FVP-only semihosting transport and minimal newlib system calls. */
#include "semihost.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
static int call(unsigned op,const void *arg){
 register unsigned r0 __asm("r0")=op;register const void *r1 __asm("r1")=arg;
 __asm volatile("bkpt 0xab":"+r"(r0):"r"(r1):"r2","r3","memory","cc");return (int)r0;
}
void diagnostic_exit(int status){uint32_t args[2]={0x20026,(uint32_t)status};call(0x20,args);for(;;)__asm volatile("wfi");}
void diagnostic_dump(const char *name,const void *data,size_t size){
 uint32_t a[3]={(uint32_t)(uintptr_t)name,5,(uint32_t)strlen(name)};int fd=call(1,a);
 if(fd<0){printf("CAPTURE_OPEN_FAILED %s\n",name);diagnostic_exit(90);}
 uint32_t w[3]={(uint32_t)fd,(uint32_t)(uintptr_t)data,(uint32_t)size};int unwritten=call(5,w);
 uint32_t c=(uint32_t)fd;int closed=call(2,&c);
 if(unwritten||closed){printf("CAPTURE_WRITE_FAILED %s %d %d\n",name,unwritten,closed);diagnostic_exit(91);}
 printf("CAPTURE %s bytes=%u\n",name,(unsigned)size);
}
/* SYS_OPEN rb=1, SYS_FLEN, SYS_READ and SYS_CLOSE: byte transport only. */
void diagnostic_read_exact(const char *name,void *data,size_t size){
 uint32_t a[3]={(uint32_t)(uintptr_t)name,1,(uint32_t)strlen(name)};int fd=call(1,a);
 if(fd<0){printf("INPUT_OPEN_FAILURE %s\n",name);diagnostic_exit(80);}
 uint32_t c=(uint32_t)fd;int length=call(0x0c,&c);
 if(length<0||(size_t)length!=size){call(2,&c);printf("INPUT_SIZE_FAILURE %s expected=%u observed=%d\n",name,(unsigned)size,length);diagnostic_exit(81);}
 uint32_t r[3]={(uint32_t)fd,(uint32_t)(uintptr_t)data,(uint32_t)size};int unread=call(6,r);
 /* Reject short reads and file growth without writing beyond staging storage. */
 uint8_t extra=0;uint32_t e[3]={(uint32_t)fd,(uint32_t)(uintptr_t)&extra,1};int eof=call(6,e);
 int after_length=call(0x0c,&c);int closed=call(2,&c);
 if(unread||eof!=1||after_length!=length){printf("INPUT_READ_FAILURE %s unread=%d eof=%d length_after=%d\n",name,unread,eof,after_length);diagnostic_exit(82);}
 if(closed){printf("INPUT_CLOSE_FAILURE %s\n",name);diagnostic_exit(83);}
 printf("RAW_INPUT_READ %s bytes=%u\n",name,(unsigned)size);
}
int _write(int fd,const char *p,int n){(void)fd;int done=0;char b[257];while(done<n){int k=n-done;if(k>256)k=256;memcpy(b,p+done,(size_t)k);b[k]=0;call(4,b);done+=k;}return n;}
void *_sbrk(ptrdiff_t n){extern char __heap_start[],__heap_end[];static char *p; if(!p)p=__heap_start;char *old=p;if(n<0||p+n>__heap_end){errno=ENOMEM;return (void*)-1;}p+=n;return old;}
void _exit(int status){diagnostic_exit(status);}
int _close(int fd){(void)fd;return -1;}
int _fstat(int fd,struct stat *s){(void)fd;s->st_mode=S_IFCHR;return 0;}
int _isatty(int fd){(void)fd;return 1;}
int _lseek(int fd,int ptr,int dir){(void)fd;(void)ptr;(void)dir;return 0;}
int _read(int fd,char *p,int n){(void)fd;(void)p;(void)n;return 0;}
int _getpid(void){return 1;}
int _kill(int pid,int sig){(void)pid;(void)sig;errno=EINVAL;return -1;}
