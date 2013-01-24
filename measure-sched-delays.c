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
#include <pthread.h>
#include <assert.h>

int64_t target_nanos;
timer_t timerid;

#define TIMER_SIG SIGRTMIN

#define BUFSIZE (1024*1024)

static int current_buffer;
static int writer_current_buffer;
static int bufpos[2];
static char buffers[2][BUFSIZE];

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
void handle_timer_tick(void)
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

static pthread_mutex_t current_buffer_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t have_data_cond = PTHREAD_COND_INITIALIZER;

void *rt_thread(void *dummy)
{
	struct sched_param sp;
	sigset_t ss;
	int rv;

	sigemptyset(&ss);
	sigaddset(&ss, TIMER_SIG);

	while (1) {
		int signo;
		sigwait(&ss, &signo);
		assert(signo == TIMER_SIG);

		pthread_mutex_lock(&current_buffer_lock);
		handle_timer_tick();
		pthread_cond_signal(&have_data_cond);
		pthread_mutex_unlock(&current_buffer_lock);
	}

}


int main()
{
	struct sigevent sev;
	struct itimerspec its;
	struct sigaction sa;
	pthread_t threadid;
	struct sched_param sp;
	sigset_t ss;
	int rv;

	sp.sched_priority = sched_get_priority_max(SCHED_FIFO);

	rv = sched_setscheduler(getpid(), SCHED_FIFO, &sp);
	if (rv < 0) {
		perror("sched_setscheduler");
		exit(1);
	}

	/* sets smaller buffer size. We're fine with passing those
	 * writes to kernel sooner */
	setvbuf(stdout, 0, _IONBF, 0);

	/* ruby effing crap sets them to ignore which childs inherit
	 * (WAT!) */
	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_handler = SIG_DFL;
	sigaction(SIGHUP, &sa, 0);
	sigaction(SIGINT, &sa, 0);
	sigaction(SIGQUIT, &sa, 0);

	memset(&sp, 0, sizeof(sp));

	rv = mlockall(MCL_CURRENT | MCL_FUTURE);
	if (rv < 0) {
		perror("mlockall");
		exit(1);
	}

	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = TIMER_SIG;
	sev.sigev_value.sival_ptr = &timerid;
	if (timer_create(CLOCK_MONOTONIC, &sev, &timerid) == -1) {
		perror("timer_create");
		exit(1);
	}

	sigemptyset(&ss);
	sigaddset(&ss, TIMER_SIG);
	sigprocmask(SIG_BLOCK, &ss, 0);

	rv = pthread_create(&threadid, 0, rt_thread, 0);
	if (rv) {
		errno = rv;
		perror("pthread_create");
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

	pthread_mutex_lock(&current_buffer_lock);
	while (1) {
		int current = current_buffer;
		int pos;

		if (!bufpos[current]) {
			pthread_cond_wait(&have_data_cond, &current_buffer_lock);
			continue;
		}

		current_buffer = current ^ 1;
		pos = bufpos[current];
		pthread_mutex_unlock(&current_buffer_lock);
		fwrite(buffers[current], 1, pos, stdout);
		bufpos[current] = 0;
		pthread_mutex_lock(&current_buffer_lock);
	}
}
