# Host test for micpcm.h, moved here from the player's texttest/ with
# the header. The rule is the one it had there.
#
#   make -C test
#
# SPDX-License-Identifier: MIT

CC ?= cc

WARNCFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter \
              -D_GNU_SOURCE -I. -I../include
CFLAGS  := -std=c11 -g -O1 -Wall -Wextra -Wno-unused-parameter \
           -fsanitize=address,undefined -fno-sanitize-recover=all \
           -fno-omit-frame-pointer -I. -I../include
LDFLAGS := -fsanitize=address,undefined -lm

all: run-micpcm

micpcmtest: micpcmtest.c ../include/micpcm.h
	$(CC) $(WARNCFLAGS) -c micpcmtest.c -o /dev/null
	$(CC) $(CFLAGS) -D_GNU_SOURCE -o $@ micpcmtest.c $(LDFLAGS)

run-micpcm: micpcmtest
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 ./micpcmtest

clean:
	rm -f micpcmtest

.PHONY: all run-micpcm clean
