#include <stdio.h>
#include <unistd.h>

int
main(void)
{
  puts("2026-10-08T09:30:00.123Z INFO pumpkin::server: Ready");
  puts("2026-10-08T09:30:01.123Z WARN pumpkin::server: Slow tick");
  puts("2026-10-08T09:30:02.123Z ERROR pumpkin::plugin: Example error");
  fflush(stdout);
  usleep(400000);
  return 0;
}
