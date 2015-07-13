/*
 * range.c:
 *      This app measures distance using HC-S04 transducer module
 *
 *	How to test:
 *      Connect the echo pin of the HC-S04 to ExitPin, and the 
 *      trigger pin of the HC-S04 to TriggerPin. Be sure not to
 *      drive the input pins of the raspberry pi with 5v of the
 *      HC-S04. A simple voltage divider can cut the voltage to 3.3v. 
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
#include "range.h"

#define TIMER_BASE 0x20003000

#define TRIGGER_PULSE_US 10 // Minimum HC-S04 trigger pulse time
#define MAX_DISTANCE_US 23307 // Max distance of HC-S04 in terms of time
#define MIN_TOTAL_MEASURE_TIME_US 75000 // Minimum HC-S04 measurement time is 60ms

int EchoPin;
int TriggerPin;
int mode_verbose;

volatile unsigned *timer;
volatile unsigned isr_time_ready;
volatile unsigned isr_distancetime;
volatile unsigned int isr_risetime, isr_falltime;
volatile unsigned int isr_error;

/*
 * EchoInterrupt:
 *********************************************************************************
 */

void EchoInterrupt (void) 
{ 	
	unsigned int current_us;
	unsigned int echopin_state;

	// Mark echo pin rise event
	isr_risetime = *timer;
	
	// Wait for echo pin fall event, or timeout
	do
	{
		current_us = *timer - isr_risetime;
		echopin_state = digitalRead(EchoPin);
	}
	while (( echopin_state == HIGH) && (current_us < MAX_DISTANCE_US));
	isr_distancetime = current_us;
	
	// If we get a time < then max capability, take it
	if (isr_distancetime < MAX_DISTANCE_US)
		isr_time_ready = 1;	
	else
		// Time is greater than our max distance, indicate an isr_error
		isr_error = 1;
}

int setup_timer()
{
	int memfd;
	void *timer_map;

	memfd = open("/dev/mem",O_RDWR|O_SYNC);
	if(memfd < 0)
	{
		printf("Mem open isr_error\n");
		return(0);
	}

	timer_map = mmap(NULL,4096,PROT_READ|PROT_WRITE,
			MAP_SHARED,memfd,TIMER_BASE);

	close(memfd);

	if(timer_map == MAP_FAILED)
	{
		printf("Map failed\n");
		return(1);
	}
              // timer pointer
	timer = (volatile unsigned *)timer_map;
	++timer;    // timer lo 4 bytes
		// timer hi 4 bytes available via *(timer+1)

	return(0);
}

int TakeMeasurement(unsigned int* feet, unsigned int* inch)
{
	unsigned int err;
	unsigned int timend;
	unsigned int isrtime;
	unsigned int measureend;
	unsigned int inches;

	// Get ready to catch interrupt
	isr_time_ready = 0;
	isr_distancetime = 0;
	isr_risetime = 0;
	isr_falltime = 0;
	isr_error = 0;
	timend = 0;
	measureend = *timer + MIN_TOTAL_MEASURE_TIME_US;

	// Trigger the transducer for TRIGGER_PULSE_US	
	digitalWrite(TriggerPin, HIGH);
	timend = *timer + TRIGGER_PULSE_US;        // TRIGGER_PULSE_US delay
//	while( (((*timer) - timend) & 0x80000000) != 0);
	while(*timer < timend) asm("nop");
	digitalWrite(TriggerPin, LOW);
	
	// Wait for interrupt
	do
		isrtime = *timer - timend;
	while ((!isr_time_ready) && (!isr_error) && (isrtime < (TRIGGER_PULSE_US + MAX_DISTANCE_US))); 

	// Check if ISR inicated an isr_error 
	if (isr_error)
		err = 2; // No end edge of echo pin
	
	// Test if we have a reasonable time
	else if (isrtime < (TRIGGER_PULSE_US + MAX_DISTANCE_US))
	{
		// Calculate the distance in SAE units
		inches = isr_distancetime / 148;
		*feet = inches / 12;
		*inch = inches % 12;
		err = 0;
	}

	// We timed out waiting for rising edge of echo pin	
	else
		err = 1; // No start edge of echo pin

	// Hang out between measurments to give the transducer time to reset
	if ((measureend - *timer) > 0)
		usleep(measureend - *timer);
	
	return(err);
}

/*
 *********************************************************************************
 * interface functions
 *********************************************************************************
 */

int RangeInit(int echopin, int triggerpin, int debug)
{
	int err = 0;
	
	EchoPin = echopin;
	TriggerPin = triggerpin;
	mode_verbose = debug;
	
	setup_timer();

	wiringPiISR (EchoPin, INT_EDGE_RISING, &EchoInterrupt) ;
	pullUpDnControl(EchoPin, PUD_DOWN);

	digitalWrite(TriggerPin, LOW);
	pinMode(TriggerPin, OUTPUT);
	
	return err;
}

double RangeMeasure(int average)
{
	unsigned int i, avgcnt;
	unsigned int feet, inch;
	int err = 0;
	unsigned int mode_repeat = 0;
	double average_val = 0;
	
	// Start measurement
	for (avgcnt = 1; 
		(avgcnt <= average) /* && (!exitpin_exit) */; 
		(mode_repeat) ? avgcnt:avgcnt++)
	{
		err = TakeMeasurement(&feet, &inch);
		 	
		// Check for an err
		switch(err)
		{
			case 0:
				average_val = ((average_val * (avgcnt - 1)) + ((feet * 12) + inch)) / avgcnt;
				if (mode_verbose)
				{
					printf("Distance: %02i ft, %02i inch, avg: %02.02f", feet, inch, average_val);
					for (i = 0; i < 40; i++) printf("\b");
				}
				break;
			case 1:
				if (mode_verbose) 
				{
					printf("isr_error: No start pulse              ");
					for (i = 0; i < 40; i++) printf("\b");
				}
				break;
			case 2:
				if (mode_verbose)
				{
					printf("isr_error: Measurement out of range     "); 
					for (i = 0; i < 40; i++) printf("\b");
				}
				break;
			default:
				printf("FUBAR!\n");
				break;
		}
	}
		
	// There is no standards here how we return to linux. For this implementation
	// I will values > 1 as distance in inches, and values < 0 as errors.
	if (err)
	{
		if (mode_verbose)
			printf("%i\n", err * -1);
		return (err * -1); // Make errors negative
	}
	else
	{
		if (mode_verbose)
			printf("%.02f\n", average_val);
		return (average_val); // Return distance in inches
	}
}


