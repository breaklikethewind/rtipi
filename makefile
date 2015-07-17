
CC=gcc
CFLAGS=-c -Wall
LDFLAGS=-lwiringPi -lpthread
SOURCES=sump.c beep.c dht_read.c range.c
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=sump

all: $(SOURCES) $(EXECUTABLE)
    
$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@
