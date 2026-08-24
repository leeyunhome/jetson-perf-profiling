#include <fcntl.h>
#include <unistd.h>

int main() {
  int fd = open("/tmp/io_bound_test.bin", O_CREAT | O_WRONLY | O_TRUNC, 0644);
  char buf[64] = {0};
  for (int i = 0; i < 2000000; i++) {
    write(fd, buf, sizeof(buf));
  }
  close(fd);
  return 0;
}
