#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"
#include "user/user.h"

static void
check(int ok, char *what)
{
  if (!ok) {
    printf("shebangtest: FAIL %s\n", what);
    exit(1);
  }
}

static void
file(char *path, char *text)
{
  int fd = open(path, O_CREATE | O_WRONLY | O_TRUNC);
  check(fd >= 0, "create");
  check(write(fd, text, strlen(text)) == strlen(text), "write");
  close(fd);
}

static void
run(char *path, char **args, int expected)
{
  int pid = fork(), status;
  check(pid >= 0, "fork");
  if (pid == 0) {
    exec(path, args);
    exit(99);
  }
  check(wait(&status) == pid && status == expected, path);
}

int
main(int argc, char **argv)
{
  // Act as an interpreter and verify argv exactly, including argv[0].
  if (argc > 1 && strcmp(argv[1], "--check") == 0) {
    check(argc == 6, "interpreter argc");
    check(strcmp(argv[0], "/shebangtest") == 0, "interpreter argv0");
    check(strcmp(argv[2], "sb-inner") == 0, "inner path");
    check(strcmp(argv[3], "sb-outer") == 0, "outer path");
    check(strcmp(argv[4], "arg1") == 0 && strcmp(argv[5], "arg two") == 0,
          "original arguments");
    exit(0);
  }

  // /echo makes the replacement argv observable without a special interpreter.
  file("sb-simple", "#!/echo\nignored body\n");
  int fds[2];
  check(pipe(fds) == 0, "pipe");
  int pid = fork(), status;
  check(pid >= 0, "fork echo");
  if (pid == 0) {
    close(fds[0]);
    close(1);
    dup(fds[1]);
    close(fds[1]);
    char *args[] = {"different argv0", "arg1", 0};
    exec("sb-simple", args);
    exit(99);
  }
  close(fds[1]);
  char buf[128];
  int n, used = 0;
  while ((n = read(fds[0], buf + used, sizeof(buf) - 1 - used)) > 0)
    used += n;
  buf[used] = 0;
  close(fds[0]);
  check(wait(&status) == pid && status == 0, "echo status");
  check(strcmp(buf, "sb-simple arg1\n") == 0, "simple argv");

  file("sb-inner", "#!  /shebangtest\t--check  \n");
  file("sb-outer", "#!sb-inner\n");
  char *args[] = {"ignored", "arg1", "arg two", 0};
  run("sb-outer", args, 0);

  file("sb-shell", "#!/sh\n# a comment\necho script-ok > sb-result\n");
  run("sb-shell", args, 0);
  int fd = open("sb-result", O_RDONLY);
  check(fd >= 0, "shell output file");
  n = read(fd, buf, sizeof(buf) - 1);
  check(n >= 0, "shell output read");
  buf[n] = 0;
  close(fd);
  check(strcmp(buf, "script-ok\n") == 0, "shell output");

  file("sb-bad", "#!/missing-interpreter\n");
  run("sb-bad", args, 99);
  file("sb-bad", "#!  \n");
  run("sb-bad", args, 99);
  file("sb-bad", "#!sb-bad\n");
  run("sb-bad", args, 99);
  memset(buf, 'x', sizeof(buf));
  buf[0] = '#';
  buf[1] = '!';
  fd = open("sb-bad", O_WRONLY | O_TRUNC);
  check(fd >= 0 && write(fd, buf, sizeof(buf)) == sizeof(buf), "long line");
  close(fd);
  run("sb-bad", args, 99);

  char *many[MAXARG];
  for (int i = 0; i < MAXARG - 1; i++)
    many[i] = "x";
  many[MAXARG - 1] = 0;
  run("sb-simple", many, 99);

  unlink("sb-simple");
  unlink("sb-inner");
  unlink("sb-outer");
  unlink("sb-shell");
  unlink("sb-result");
  unlink("sb-bad");
  printf("shebangtest: OK\n");
  exit(0);
}
