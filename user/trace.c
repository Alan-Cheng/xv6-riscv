#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  char *args[MAXARG];
  uint mask = 0;

  if (argc < 3 || argc - 2 >= MAXARG || argv[1][0] == 0) {
    fprintf(2, "usage: trace mask command [args ...]\n");
    exit(1);
  }

  for (char *s = argv[1]; *s; s++) {
    if (*s < '0' || *s > '9' ||
        mask > (2147483647U - (*s - '0')) / 10) {
      fprintf(2, "trace: mask must be an integer from 0 to 2147483647\n");
      exit(1);
    }
    mask = mask * 10 + (*s - '0');
  }

  if (trace(mask) < 0) {
    fprintf(2, "trace: failed to enable tracing\n");
    exit(1);
  }

  int i;
  for (i = 2; i < argc; i++)
    args[i - 2] = argv[i];
  args[i - 2] = 0;
  exec(args[0], args);
  fprintf(2, "trace: exec %s failed\n", args[0]);
  exit(1);
}
