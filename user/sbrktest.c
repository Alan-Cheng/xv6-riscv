#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  char *old_break = sbrk(1);
  if (old_break == SBRK_ERROR) {
    fprintf(2, "sbrktest: sbrk(1) failed\n");
    exit(1);
  }
  printf("sbrktest: sbrk(1) returned %p\n", old_break);
  exit(0);
}
