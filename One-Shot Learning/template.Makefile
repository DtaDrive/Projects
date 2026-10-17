CC = gcc
CFLAGS = -g -std=c99 -Wall -Wvla -Werror -fsanitize=address,undefined

estimate: estimate.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -rf estimate *.o *.dSYM
