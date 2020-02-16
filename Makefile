CFLAGS = -Wall -Werror -std=c99 -DLINUX
CFKAGS += -g
#CXXFLAGS += -DNDEBUG
LDLIBS =

#CC = gcc
CC = clang

PRGM  = lisp-c
SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:.c=.o)
DEPS := $(OBJS:.o=.d)

.PHONY: all clean

all: $(PRGM)

$(PRGM): $(OBJS)
	$(CC) $(OBJS) $(LDLIBS) -o $@

%.o: %.cpp
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -rf $(OBJS) $(DEPS) $(PRGM)

-include $(DEPS)
