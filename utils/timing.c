#include <time.h>

long time_mili(void) {
	struct timespec tm;
	clock_gettime(CLOCK_REALTIME, &tm);

	return tm.tv_sec * 1000 + tm.tv_nsec / 1000000;
}