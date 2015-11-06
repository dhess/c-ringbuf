CC=clang
CFLAGS=-O0 -g -Wall -Wpointer-arith -ftrapv -fsanitize=undefined-trap -fsanitize-undefined-trap-on-error

# or, for gcc...
#CC=gcc
#CFLAGS=-O0 -g -Wall

LD=$(CC)
LDFLAGS=-g

test:	ringbuf-test
	./ringbuf-test

coverage: ringbuf-test-gcov
	  ./ringbuf-test-gcov
	  gcov -o ringbuf-gcov.o ringbuf.c

valgrind: ringbuf-test
	  valgrind ./ringbuf-test

help:
	@echo "Targets:"
	@echo
	@echo "test  - build and run ringbuf unit tests."
	@echo "coverage - use gcov to check test coverage of ringbuf.c."
	@echo "valgrind - use valgrind to check for memory leaks."
	@echo "clean - remove all targets."
	@echo "help  - this message."

ringbuf-test-gcov: ringbuf-test-gcov.o ringbuf-gcov.o
	gcc -o ringbuf-test-gcov --coverage $^

ringbuf-test-gcov.o: ringbuf-test.c ringbuf.h
	gcc -c $< -o $@

ringbuf-gcov.o: ringbuf.c ringbuf.h
	gcc --coverage -c $< -o $@

ringbuf-test: ringbuf-test.o ringbuf.o
	$(LD) -o ringbuf-test $(LDFLAGS) $^

ringbuf-test.o: ringbuf-test.c ringbuf.h
	$(CC) $(CFLAGS) -c $< -o $@

ringbuf.o: ringbuf.c ringbuf.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f ringbuf-test ringbuf-test-gcov *.o *.gcov *.gcda *.gcno

.PHONY:	clean
