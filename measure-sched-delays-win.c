#define _WIN32_WINNT _WIN32_WINNT_LONGHORN
#define WINVER _WIN32_WINNT_LONGHORN

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <windows.h>
#include <process.h>

int64_t target_nanos;

#define BUFSIZE (1024*1024)

static int current_buffer;
static int bufpos[2];
static char buffers[2][BUFSIZE];

static CRITICAL_SECTION current_buffer_lock;
static CONDITION_VARIABLE have_data_cond;

static double perf_counts_per_nsec_inv;

static
int64_t read_nanos(void)
{
	LARGE_INTEGER li;
	BOOL rv = QueryPerformanceCounter(&li);
	if (!rv) {
		abort();
	}
	return (int64_t)((double)(li.QuadPart) * perf_counts_per_nsec_inv);
}

static
int int_min(int a, int b)
{
	return a > b ? b : a;
}

unsigned WINAPI rt_thread(void *_dummy)
{
	HANDLE timer;
	BOOL rv;
	LARGE_INTEGER due_time;
	char *stuff;

	timer = CreateWaitableTimer(NULL, FALSE, NULL);

	if (timer == NULL) {
		abort();
	}

	due_time.QuadPart = -10000000;
	rv = SetWaitableTimer(timer, &due_time, 1000, NULL, NULL, FALSE);
	if (!rv) {
		abort();
	}

	rv = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
	if (!rv) {
		abort();
	}

	while (1) {
		DWORD wrv = WaitForSingleObject(timer, INFINITE);
		int64_t nanos_now;
		int64_t nanos_after;
		if (wrv != WAIT_OBJECT_0) {
			abort();
		}

		nanos_now = read_nanos();

		if (target_nanos == 0) {
			target_nanos = nanos_now + 1000000000;
			continue;
		}

		stuff = (char *)VirtualAlloc(NULL, 4096, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
		if (stuff == NULL) {
			abort();
		}
		stuff[0] ^= 1;

		nanos_after = read_nanos();

		VirtualFree(stuff, 4096, MEM_RELEASE);

		EnterCriticalSection(&current_buffer_lock);
		{
			long long diff = (long long)(nanos_now - target_nanos);
			long long diff2 = (long long)(nanos_after - nanos_now);

			int current = current_buffer;
			int pos = bufpos[current];

			int srv;

			if (pos > BUFSIZE - 1024) {
				fprintf(stderr, "%f: exceeded bufsize!\n", (double)nanos_now);
				goto out;
			}

			srv = _snprintf(buffers[current] + pos, BUFSIZE - pos,
					   "%.9f %ld %ld\n",
					   nanos_now * (double)1E-9, (long)diff, (long)diff2);
			bufpos[current] = int_min(pos + srv, BUFSIZE);

			target_nanos = nanos_now + 1000000000LL;
		}
	out:
		LeaveCriticalSection(&current_buffer_lock);
		WakeConditionVariable(&have_data_cond);
	}

}

static
void setup_perf_freq(void)
{
	LARGE_INTEGER li;
	BOOL rv;

	rv = QueryPerformanceFrequency(&li);
	if (!rv) {
		abort();
	}

	perf_counts_per_nsec_inv = 1E9 / (double)(li.QuadPart);
}

int main()
{
	int rv;

	setup_perf_freq();

	timeBeginPeriod(10);

	/* sets smaller buffer size. We're fine with passing those
	 * writes to kernel sooner */
	setvbuf(stdout, 0, _IONBF, 0);

	InitializeConditionVariable(&have_data_cond);
	InitializeCriticalSection(&current_buffer_lock);

	rv = (int)_beginthreadex(NULL, 0, rt_thread, NULL, 0, NULL);
	if (rv == 0) {
		abort();
	}


	EnterCriticalSection(&current_buffer_lock);
	while (1) {
		int current = current_buffer;
		int pos;

		if (!bufpos[current]) {
			SleepConditionVariableCS(&have_data_cond, &current_buffer_lock, INFINITE);
			continue;
		}

		current_buffer = current ^ 1;
		pos = bufpos[current];
		LeaveCriticalSection(&current_buffer_lock);
		fwrite(buffers[current], 1, pos, stdout);
		bufpos[current] = 0;
		EnterCriticalSection(&current_buffer_lock);
	}
}
