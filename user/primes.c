#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include <stdbool.h>
void child_function(int fd[2]) {
  //close write end
  close(fd[1]);
  //read input
  int num_bytes_read;
  int prime = -1;
  int fd2[2];
  int buf2;
  int child_pid;
  num_bytes_read = read(fd[0], &buf2, sizeof(int));
  if(num_bytes_read <= 0) {
    close(fd[0]);
    return;
  }
  prime = buf2;
  if(prime > 31) {
    close(fd[0]);
    return;
  }
  printf("prime %d\n", prime);
  if(pipe(fd2) < 0) {
    close(fd[0]);
    printf("error due to pipe in child function\n");
    exit(1);
  }
  child_pid = fork();
  if(child_pid == 0) {
    child_function(fd2);
  } else {
    // close read end
    close(fd2[0]);
    while(true) {
      num_bytes_read = read(fd[0], &buf2, sizeof(int));
      if(num_bytes_read == 0) {
        // parent has finished writing.
        // let us close our write end and wait on child
        close(fd2[1]);
        close(fd[0]);
        wait(0);
        return;
      }
      if(buf2 % prime == 0) continue;
      write(fd2[1], &buf2, sizeof(int));
    }
  }
}

int main(int argc, char* argv[]) {
  int i, child_pid;
  int fd[2];
  if(pipe(fd) < 0){
    printf("error due to pipe\n");
    exit(1);
  }
  child_pid = fork();
  if(child_pid == 0) {
    // close write end
    // read input
    // filter
    // write to right neighbour
    child_function(fd);
  } else {
    //close read end of pipe
    close(fd[0]);
    printf("fd1: %d\n", fd[1]);
    for(i= 2; i <= 35; ++i) {
      write(fd[1], &i, sizeof(int));
    }
    // close write end of pipe
    close(fd[1]);
    wait(0);
  }
  exit(0);
}
