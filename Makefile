################################################################################
# Makefile                                                                     #
#                                                                              #
# Description: This file contains the make rules for Recitation 1.             #
#                                                                              #
# Authors: Athula Balachandran <abalacha@cs.cmu.edu>,                          #
#          Wolf Richter <wolf@cs.cmu.edu>                                      #
#                                                                              #
################################################################################



CC=gcc
CFLAGS=-I.
DEPS = parse.h y.tab.h log.h
OBJ = y.tab.o lex.yy.o log.o parse.o liso.o
FLAGS = -g -Wall

default:all

all: liso

lex.yy.c: lexer.l
	flex $^

y.tab.c: parser.y
	yacc -d $^

%.o: %.c $(DEPS)
	$(CC) $(FLAGS) -c -o $@ $< $(CFLAGS)

liso: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f *~ *.o example lex.yy.c y.tab.c y.tab.h