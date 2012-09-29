bin_PROGRAMS = measure-sched-delays
measure_sched_delays_SOURCES = measure-sched-delays.c

bin_SCRIPTS = collect-sched-delays.rb

EXTRA_DIST = collect-sched-delays.rb

measure_sched_delays_LDADD = -lrt

check-syntax:
	$(COMPILE.c) -o /dev/null -S ${CHK_SOURCES}

%.s : %.c
	$(COMPILE.c) -S -fverbose-asm -o $@ $<
