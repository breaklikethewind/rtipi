/*
 * sump.c:
 *      This app manages the sump pump
 *
 *
 * Copyright (c) 2014 Eric Nelson
 ***********************************************************************
 * This file uses wiringPi:
 *	https://projects.drogon.net/raspberry-pi/wiringpi/
 *
 *    wiringPi is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Lesser General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    wiringPi is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public License
 *    along with wiringPi.  If not, see <http://www.gnu.org/licenses/>.
 ***********************************************************************
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <wiringPi.h>
#include <sys/mman.h>
#include <time.h>
#include "beep.h"
#include "range.h"

#define BeepPin 2 // Raspberry pi gpio27
#define TempPin 5 // GPIO 24
#define EchoPin 7 // Raspberry pi gpio4
#define TriggerPin 0 // Raspberry pi gpio 17

void printhelp(void)
{
	printf("\n");
	printf("This utility reads the HC-S04 transducer device. The distance\n");
	printf("in inches is provided in the return value. Negative return values\n");
	printf("indicate an error.\n");
	printf("\n");
	printf("Options:\n");
	printf(" -d debug the sensor by repeatedly read & display distance\n");
	printf(" -h This help screen\n");
	printf(" -a n Average n samples & provide the average result\n");
	printf("\n");
	printf("Return values: \n");
	printf(" > 0: Distance measured in inches\n");
	printf("  -1: The echo pin on the HC-S04 did not have a rising edge\n");
	printf("  -2: The echo pin on the HC-S04 did not have a falling edge. This\n");
	printf("      can be due to a distance too far to measure, a soft target, the\n");
	printf("      target is too small, or the target is not perpandicular to the\n");
	printf("      HC-S04\n");
	printf("\n");
}

/*
 *********************************************************************************
 * main
 *********************************************************************************
 */

int main (int argc,char *argv[])
{
	int err = 0;
	int opt = 0;
	time_t t;

	while ((opt = getopt(argc, argv, "h::")) != -1) 
	{
		switch(opt) 
		{
			case 'h':
				// hoption = optarg;
				printhelp();
				break;
			case '?':
				// if (optopt == 'd') 
				//	printf("\nMissing mandatory input option");
				// else if (optopt == 'h')
				//	printf("\nMissing mandatory output option");
				// else
				//	printf("\nInvalid option received");
				printf("Use 'morse -h' for usage information\n");
				break;
		}
	}
	
	// Setup GPIO's, Timers, Interrupts, etc
	wiringPiSetup() ;

//	TempInit(TempPin);
	BeepInit(BeepPin, 0);
	RangeInit(EchoPin, TriggerPin, 0);
	
	BeepMorse(5, "Hello World");
	printf("Distance: %.02f\n", RangeMeasure(5));
	
	return err;
}
