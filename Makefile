CC := clang

mmuniq: *c Makefile
	$(CC) \
		-g -ggdb -O3 \
		-Wall -Wextra -Wpointer-arith \
		-D_FORTIFY_SOURCE=2 -fPIE \
		mmuniq.c \
		-lm \
		-Wl,-z,now -Wl,-z,relro \
		-o mmuniq


.PHONY: format
format:
	clang-format -i *.c
