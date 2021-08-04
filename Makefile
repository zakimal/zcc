CFLAGS=-std=c11 -g -static
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

zcc: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJS): zcc.h

test: zcc
	./test.sh
	./test-driver.sh

clean:
	rm -f zcc *.o *~ tmp*

.PHONY: test clean