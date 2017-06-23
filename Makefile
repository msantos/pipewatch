.PHONY: all clean test

RM ?= rm

CFLAGS ?= -D_FORTIFY_SOURCE=2 -O2 -fstack-protector \
		  --param=ssp-buffer-size=4 -Wformat -Werror=format-security \
		  -fno-strict-aliasing

PIPEWATCH_CFLAGS ?= -g -Wall

CFLAGS += $(PIPEWATCH_CFLAGS)

all: pipewatch test

pipewatch:
	$(CC) $(CFLAGS) -o pipewatch pipewatch.c -lpipeline

clean:
	-@$(RM) pipewatch

test: pipewatch
	@PATH=.:$(PATH) bats test
