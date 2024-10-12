#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char* argv[])
{
  int fd[2];
  int child_pid;
  int status = pipe(fd);
  if(status < 0) {
    fprintf(2,"failed to init pipe\n");
    //exit(1);
  }
  child_pid = fork();
  if(child_pid== 0) {
    // child_pid
    // block on read
    char c;
    status = read(fd[0], &c, 1);
    printf("%d: received ping\n", getpid());
    // write to parent
    write(fd[1], &c, 1);
  } else {
    // parent
    // write to child
    char c = 'a';
    write(fd[1], &c, 1);
    // wait to read from child
    status = read(fd[0], &c, 1);
    printf("%d: received pong\n", getpid());
    close(fd[0]);
    close(fd[1]);
  }
  exit(0);
}
