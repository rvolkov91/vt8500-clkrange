gccflags=-Wall
ami_headers=./include/ami-unreleased

all: main.c
	gcc $(gccflags) -o vt8500-clkrange main.c

clean:
	rm -f vt8500-clkrange