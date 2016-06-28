LDLIBS=-ldvdread -ldvdcss
dvdread:	dvdread.c
clean:
	rm -f dvdread *.o
