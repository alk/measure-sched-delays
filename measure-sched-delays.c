#define _GNU_SOURCE
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sched.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/mman.h>

int64_t target_nanos;
timer_t timerid;

#define BUFSIZE (1024*1024)

volatile sig_atomic_t current_buffer;
volatile sig_atomic_t bufpos[2];
char buffers[2][BUFSIZE];

static
int64_t read_nanos(int type)
{
	struct timespec ts;
	int rv = clock_gettime(type, &ts);

	if (rv < 0) {
		perror("clock_gettime");
		abort();
	}

	return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static
int int_min(int a, int b)
{
	return a > b ? b : a;
}

static
void signal_handler(void)
{
	int64_t nanos = read_nanos(CLOCK_MONOTONIC);
	int current = current_buffer;
	int pos = bufpos[current];

	if (pos > BUFSIZE - 1024) {
		fprintf(stderr, "%lld exceeded bufsize!\n", (long long)nanos);
		return;
	}

	long long diff = (long long)(nanos - target_nanos);
	int srv = snprintf(buffers[current] + pos, BUFSIZE - pos,
			   "%.9f %lld\n",
			   read_nanos(CLOCK_REALTIME) * (double)1E-9, diff);
	bufpos[current] = int_min(pos + srv, BUFSIZE);

	target_nanos += 1000000000LL;
}

static
void mysigsuspend(void)
{
	sigset_t new;
	int rv;
	rv = sigprocmask(SIG_BLOCK, 0, &new);
	if (rv < 0) {
		perror("sigprocmask");
		exit(1);
	}
	rv = sigsuspend(&new);
	if (rv < 0 && errno != EINTR) {
		perror("sigsuspend");
		exit(1);
	}
}


int main()
{
	struct sigevent sev;
	struct itimerspec its;
	struct sigaction sa;
	int rv;

	struct sched_param sp;

	/* sets smaller buffer size. We're fine with passing those
	 * writes to kernel sooner */
	setvbuf(stdout, 0, _IONBF, 0);

	/* ruby effing crap sets them to ignore which childs inherit
	 * (WAT!) */
	sa.sa_handler = SIG_DFL;
	sigaction(SIGHUP, &sa, 0);
	sigaction(SIGINT, &sa, 0);
	sigaction(SIGQUIT, &sa, 0);

	memset(&sp, 0, sizeof(sp));

	sp.sched_priority = sched_get_priority_max(SCHED_FIFO);

	rv = sched_setscheduler(getpid(), SCHED_FIFO, &sp);
	if (rv < 0) {
		perror("sched_setscheduler");
		exit(1);
	}

	rv = mlockall(MCL_CURRENT | MCL_FUTURE);
	if (rv < 0) {
		perror("mlockall");
		exit(1);
	}

	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_flags = SA_SIGINFO | SA_RESTART;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGRTMIN);
	sa.sa_sigaction = (void *)signal_handler;

	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGRTMIN;
	sev.sigev_value.sival_ptr = &timerid;
	if (timer_create(CLOCK_MONOTONIC, &sev, &timerid) == -1) {
		perror("timer_create");
		exit(1);
	}

	rv = sigaction(SIGRTMIN, &sa, 0);
	if (rv) {
		perror("sigaction");
		exit(1);
	}

	rv = clock_gettime(CLOCK_MONOTONIC, &its.it_value);
	if (rv < 0) {
		perror("clock_gettime");
		abort();
	}
	its.it_value.tv_sec += 2;
	its.it_value.tv_nsec = 0;
	its.it_interval.tv_sec = 1;
	its.it_interval.tv_nsec = 0;
	target_nanos = 1000000000LL * its.it_value.tv_sec + its.it_value.tv_nsec;

	rv = timer_settime(timerid, TIMER_ABSTIME, &its, 0);
	if (rv < 0) {
		perror("timer_settime");
		exit(1);
	}

	while (1) {
		int current = current_buffer;
		if (bufpos[current]) {
			int pos;
			current_buffer = current ^ 1;
			pos = bufpos[current];
			fwrite(buffers[current], 1, pos, stdout);
			bufpos[current] = 0;
			continue;
		}
		mysigsuspend();
	}
}
