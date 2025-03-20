#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int main(int argc, char *argv[]) {
  int status;
  for (int i = 0; i < 10; i++) {
    int pid = fork();
    if (pid < 0) {
      fprintf(2, "fork failed\n");
      exit(1);
    }

    if (pid == 0) {
      printf("child %d created\n", getpid());
      exit(0);
    }
  }

  for (int i = 0; i < 10; i++) {
    wait(&status);
  }

  printf("All children exited successfully.\n");

  exit(0);
}
