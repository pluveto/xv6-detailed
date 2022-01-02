#include "types.h"
#include "stat.h"
#include "user.h"

static void
putc(int fd, char c)
{
  write(fd, &c, 1);
}

static void
printint(int fd, int xx, int base, int sgn)
{
  static char digits[] = "0123456789ABCDEF";
  char buf[16];
  int i, neg;
  uint x;

  neg = 0;
  if(sgn && xx < 0){
    neg = 1;
    x = -xx;
  } else {
    x = xx;
  }

  i = 0;
  do{
    buf[i++] = digits[x % base];
  }while((x /= base) != 0);
  if(neg)
    buf[i++] = '-';

  while(--i >= 0)
    putc(fd, buf[i]);
}

// Print to the given fd. Only understands %d, %x, %p, %s.
void
printf(int fd,const char *fmt, ...)
{
  char *s;
  int c, i, state;
  uint *ap;

  state = 0;
  ap = (uint*)(void*)&fmt + 1;
  for(i = 0; fmt[i]; i++){
    c = fmt[i] & 0xff;
    if(state == 0){
      if(c == '%'){
        state = '%';
      } else {
        putc(fd, c);
      }
    } else if(state == '%'){
      if(c == 'd'){
        printint(fd, *ap, 10, 1);
        ap++;
      } else if(c == 'x' || c == 'p'){
        printint(fd, *ap, 16, 0);
        ap++;
      } else if(c == 's'){
        s = (char*)*ap;
        ap++;
        if(s == 0)
          s = "(null)";
        while(*s != 0){
          putc(fd, *s);
          s++;
        }
      } else if(c == 'c'){
        putc(fd, *ap);
        ap++;
      } else if(c == '%'){
        putc(fd, c);
      } else {
        // Unknown % sequence.  Print it to draw attention.
        putc(fd, '%');
        putc(fd, c);
      }
      state = 0;
    }
  }
}

#define MAX_COUNT 3

int apple = 0;

void father(){
  for (int i = 0; i < 100; i++)
  {
    // printf(1, "hello from child 1\n");
    if(apple < MAX_COUNT) {
      apple++;
      printf(1, "father: current apple count: %d\n", apple);
    }
  }
}

void son(){
  for (int i = 0; i < 100; i++)
  {
    // printf(1, "hello from child 2\n");
    if(apple > 0) {
      // apple--;
      printf(1, "son: current apple count: %d\n", apple);
    }
  }
}


void two_proc_test(){
  int pid1,pid2;
  pid1 = fork();
  if(pid1 == 0) {
    father();
    exit();
  }
  pid2 = fork();
  if(pid2 == 0) {
    son();
    exit();
  }
  wait();
  wait();
  printf(1, "final apple count: %d\n", apple);
  printf(1, "all children process finished.\n");
}
int
main(void)
{
  // forktest();
  two_proc_test();
  exit();
}
