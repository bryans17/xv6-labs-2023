#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"
#include <stdbool.h>

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(2, "usage: xargs cmd args\n");
    exit(1);
  }
  int argcount = 0;
  char* args[MAXARG];
  for(int i = 1; i < argc; ++i, ++argcount) {
    args[argcount] = argv[i];
  }
  int child_pid;
  char buf[512];
  char byte;
  int bytes_read = 1;
  while(bytes_read) {
    int ptr = 0, nptr = 0;
    while(true) {
      bytes_read = read(0, &byte, 1); 
      if(bytes_read == 0) {
        exit(0);
      }
      if(byte == '\n'){
        args[argcount++]=&buf[nptr];
        nptr = ptr;
        break;
      } else if (byte == ' '){
        buf[ptr++]=0;
        args[argcount++]=&buf[nptr];
        nptr=ptr;
      }else{
        buf[ptr++] = byte;
      }
    }
    child_pid = fork();
    if(child_pid == 0) {
      exec(args[0], args);
    } else {
      wait(0);
    }
  }
  exit(0);
}
