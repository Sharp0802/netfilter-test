
CC	:= gcc
LD	:= ld
MK	:= mkdir -p
RM	:= rm -rf
ACL	:= chmod

SRC	:= $(wildcard *.c)
OBJ	:= $(SRC:.c=.o)

DEP	:= netfilter_queue c
DEP	:= $(addprefix -l,$(DEP))

CFLAGS	+= -std=c89 -O2 -W -Wall -Wextra
LDFLAGS	+= $(DEP)

all: $(OBJ)
	$(CC)	$(LDFLAGS) $(OBJ) -o netfilter-test

%.o: %.c
	$(CC)	$(CFLAGS) `libnet-config --defines` `libnet-config --libs` -c $^ -o $@

clean:
	rm -rf *.o

