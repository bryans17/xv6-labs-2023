#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include <stdbool.h>

void find(char* path, char* pattern) {
  // open file
  int fd;
  char buf[512], *p;
  struct stat st;
  struct dirent de;
  if((fd = open(path, O_RDONLY)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot state %s\n", path);
    close(fd);
    return;
  }

  if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
    printf("find: path too long\n");
    return;
  }
  strcpy(buf, path);
  p = buf+strlen(buf);
  *p++ = '/';
  while(read(fd, &de, sizeof(de)) == sizeof(de)) {
    if(de.inum == 0) continue;
    memmove(p, de.name, DIRSIZ);
    p[DIRSIZ] = 0;
    if(stat(buf, &st) < 0) {
      printf("find: cannot stat %s\n", buf);
      continue;
    }
    if(st.type == T_DIR && strcmp(p, ".") != 0 && strcmp(p, "..") != 0){
      //printf("directory name is: %s\n", buf);
      char n[512];
      strcpy(n, buf);
      find(n, pattern);
    }
    if(st.type == T_FILE && strcmp(p, pattern) == 0) {
      printf("%s\n", buf);
    }
  }
}

int main(int argc, char* argv[]){
  char* path, *pattern;
  if(argc <= 2) {
    fprintf(2, "usage: find dir pattern\n");
    exit(1);
  }
  path = argv[1];
  pattern = argv[2];
  find(path, pattern);
  exit(0);
}
