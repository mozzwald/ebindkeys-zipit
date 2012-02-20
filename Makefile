# Makefile for ebindkeys
CC?=gcc
CFLAGS=-L. -L.. -O2 --std=c99 -lpthread
LIBS=


all:
	$(CC) $(CFLAGS) -c *.c
	$(CC) $(CFLAGS) *.o -o ebindkeys $(LIBS)
	
clean:
	rm -f *.o ebindkeys
