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
#include <stdbool.h>
#include <unistd.h>
#include <termios.h>
#include <pthread.h>
#include <fcntl.h>
#include <wiringPi.h>
#include <sys/mman.h>
#include <time.h>
#include <arpa/inet.h>
#include <signal.h>

#include <sys/socket.h>
#include <netinet/in.h>

#include "beep.h"
#include "range.h"
#include "dht_read.h"
#include "transport.h"

#define BeepPin 2 // Raspberry pi gpio27
#define EchoPin 7 // Raspberry pi gpio4
#define TriggerPin 0 // Raspberry pi gpio 17
#define DHTPin 5 // GPIO 24

#define DEFAULT_SENSOR_PERIOD 60 // Seconds

typedef struct
{
	float humidity_pct;
	float temp_f;
	float distance_in;
	int beeper;
	char morse[80];
} status_t;

status_t status;
int sensor_period = DEFAULT_SENSOR_PERIOD;
int exitflag = 0;
pthread_mutex_t lock; // sync between UDP thread and main
commandlist_t command_list;
void *thread_sensor_sample( void *ptr );
void measure(void);

typedef int (*cmdfunc)(char* request, char* response);

int morse(char* request, char* response); 
int app_exit(char* request, char* response);

commandlist_t commandlist[] = { 
{ "GETHUMIDITY",     "HUMIDITY",     NULL,      TYPE_FLOAT,   &status.humidity_pct}, 
{ "GETTEMP",         "TEMP",         NULL,      TYPE_FLOAT,   &status.temp_f}, 
{ "GETDISTANCE",     "DISTANCE",     NULL,      TYPE_FLOAT,   &status.distance_in},
{ "GETBEEPER",       "BEEPER",       NULL,      TYPE_INTEGER, &status.beeper},
{ "DOMORSE",         "MORSE",        &morse,    TYPE_STRING,  NULL},
{ "SETSENSORPERIOD", "SENSORPERIOD", NULL,      TYPE_INTEGER, &sensor_period},
{ "EXIT",            "EXIT",         &app_exit, TYPE_INTEGER, &exitflag},
{ "",                "",             NULL,      TYPE_NULL,    NULL}
};
 
pushlist_t pushlist[] = { \
{ "HUMIDITY", TYPE_FLOAT,   &status.humidity_pct}, 
{ "TEMP",     TYPE_FLOAT,   &status.temp_f}, 
{ "DISTANCE", TYPE_FLOAT,   &status.distance_in},
{ "BEEPER",   TYPE_INTEGER, &status.beeper},
{ "",         TYPE_NULL,          NULL} 
};

int morse(char* request, char* response) 
{
	BeepMorse(5, request);
	strcpy(response, request);
	return 0;
}

int app_exit(char* request, char* response)
{
	exitflag = 1;
	sprintf(response, "1");
	
	return 0;
}

void *thread_sensor_sample( void *ptr ) 
{
	
	while (!exitflag)
	{
		measure();
		sleep(sensor_period);
	}
	
	return NULL;
}

void measure( void )
{
	float temp_c;

	// Fetch sensor data
	pthread_mutex_lock(&lock);
	status.distance_in = RangeMeasure(5);
	pthread_mutex_unlock(&lock);
		
	pthread_mutex_lock(&lock);
	dht_read_val(&status.temp_f, &temp_c, &status.humidity_pct);
	pthread_mutex_unlock(&lock);
}

/*
 *********************************************************************************
 * main
 *********************************************************************************
 */

int  main(void)
{
	int  iret1;
	pthread_t sensor_sample;

	// Setup GPIO's, Timers, Interrupts, etc
	wiringPiSetup() ;

	BeepInit(BeepPin, 0);
	RangeInit(EchoPin, TriggerPin, 0);
	dht_init(DHTPin);
	
	iret1 = pthread_mutex_init(&lock, NULL); 
	if(iret1)
	{
		BeepMorse(5, "Mutex Fail");
		fprintf(stderr,"Error - mutex init failed, return code: %d\n",iret1);
		return -1;
	}

	tp_handle_requests(commandlist, &lock);
	
	tp_handle_data_push(pushlist, &lock);

	iret1 = pthread_create( &sensor_sample, NULL, thread_sensor_sample, NULL);
	if(iret1)
	{
		fprintf(stderr,"Error - pthread_create() return code: %d\n",iret1);
		BeepMorse(5, "thread_sensor_sample Thread Create Fail");
		return -2;
	}
	else
		printf("Launching thread sensor_sample\r\n");

	BeepMorse(5, "OK");
	
	while (!exitflag);
	
	printf("Sump Exit Set...\r\n");
	
	// Exit	
	tp_stop_handlers();
	pthread_join(sensor_sample, NULL);
	pthread_mutex_destroy(&lock);

	BeepMorse(5, "Exit");
	
	return 0;
}

