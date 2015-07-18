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

#include <sys/socket.h>
#include <netinet/in.h>

#include "beep.h"
#include "range.h"
#include "dht_read.h"

#define BeepPin 2 // Raspberry pi gpio27
#define EchoPin 7 // Raspberry pi gpio4
#define TriggerPin 0 // Raspberry pi gpio 17
#define DHTPin 5 // GPIO 24

#define HOST_IP "192.168.1.101"

#define DEFAULT_PUSH_PERIOD 30 // Seconds
#define DEFAULT_SENSOR_PERIOD 60 // Seconds

typedef struct
{
	unsigned int humidity_alarm_setpoint;
	unsigned int temp_alarm_setpoint;
	unsigned int distance_alarm_setpoint;
	bool beeper_state;
	char beeper_message[128];
	int push_period;
	int sensor_period;
} control_t;

typedef struct
{
	float humidity_pct;
	float temp_f;
	float distance_in;
	bool beeper;
} status_t;

struct sockaddr_in servaddr,cliaddr;
control_t control;
status_t status;
pthread_mutex_t lock; // sync between UDP thread and main
void *thread_data_push( void *ptr );
void *thread_request_handler( void *ptr );
void *thread_sensor_sample( void *ptr );
bool sump_exit = false;
bool paired = false;

void *thread_request_handler( void *ptr ) 
{
	int sockfd, n;
	socklen_t len;
	char mesg[1000];
	char sendmesg[1000] = {0};

	sockfd=socket(AF_INET, SOCK_DGRAM, 0);

	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
	servaddr.sin_port=htons(32000);
	bind(sockfd,(struct sockaddr *)&servaddr,sizeof(servaddr));

	while (!sump_exit)
	{
		len = sizeof(cliaddr);
		n = recvfrom(sockfd, mesg, 1000, 0, (struct sockaddr *)&cliaddr, &len);
		printf("-------------------------------------------------------\n");
		mesg[n] = 0;
		printf("Received the following:\n");
		printf("%s",mesg);
		printf("-------------------------------------------------------\n");

		if (strncmp(mesg, "GETHUMIDITY", 11) == 0) 
		{
			pthread_mutex_lock(&lock);
			sprintf(sendmesg, "HUMIDITY=%.1f\r\n", status.humidity_pct);
			pthread_mutex_unlock(&lock);
			sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
		}
		else if (strncmp(mesg, "GETTEMP", 7) == 0) 
		{
			pthread_mutex_lock(&lock);
			sprintf(sendmesg, "TEMP=%.1f\r\n", status.temp_f);
			pthread_mutex_unlock(&lock);
			sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
		}
		else if (strncmp(mesg, "GETDISTANCE", 11) == 0) 
		{
			pthread_mutex_lock(&lock);
			sprintf(sendmesg, "DISTANCE=%.1f\r\n", status.distance_in);
			pthread_mutex_unlock(&lock);
			sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
		}
		else if (strncmp(mesg, "GETBEEPER", 15) == 0) 
		{
			pthread_mutex_lock(&lock);
			sprintf(sendmesg, "BEEPER=%s\r\n", (status.beeper) ? "on":"off");
			pthread_mutex_unlock(&lock);
			sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
		}
		else if (strncmp(mesg, "SHUTDOWN", 8) == 0)
		{
			pthread_mutex_lock(&lock);
			sprintf(sendmesg, "SHUTDOWN=true\r\n");
			pthread_mutex_unlock(&lock);
			sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
			sump_exit = true;          
		}
		else if (strncmp(mesg, "SETPUSHPERIOD", 13) == 0)
		{
			pthread_mutex_lock(&lock);
			if (mesg[13] == '=')
			{
				control.push_period = atol(mesg + 14);
				sprintf(sendmesg, "PUSHPERIOD=%u\r\n", control.push_period);
				pthread_mutex_unlock(&lock);
				sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
				if (!paired)
				{
					printf("Paired!, Push Period Set: %u\r\n", control.push_period);
					paired = true;
				}
			}
		}
		else if (strncmp(mesg, "SETSENSORPERIOD", 15) == 0)
		{
			pthread_mutex_lock(&lock);
			if (mesg[15] == '=')
			{
				control.sensor_period = atol(mesg + 16);
				sprintf(sendmesg, "SENSORPERIOD=%u\r\n", control.sensor_period);
				pthread_mutex_unlock(&lock);
				sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
			}
		}
		else 
		{
			sprintf(sendmesg, "INVALID COMMAND\r\n");
			sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
		}
	}
	
	return NULL;
}

void *thread_data_push( void *ptr ) 
{
	int sockfd;
	char sendmesg[1000] = {0};
		
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	inet_pton(AF_INET, HOST_IP, (void*)&cliaddr.sin_addr.s_addr);
	
	while (!sump_exit)
	{
		if (!paired)
		{
			sprintf(sendmesg, "PUSHPERIOD=0\r\n");
			sendto(sockfd, sendmesg, sizeof(sendmesg), 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr));
			printf("Sending 'PUSHPERIOD=0' to %s, to establish pairing\r\n", HOST_IP);
		}
		else
		{
			printf("Pushing data...\r\n");
			
			// Send sensor data to host
			pthread_mutex_lock(&lock);
			sprintf(sendmesg, "HUMIDITY=%.1f\r\n", status.humidity_pct);
			pthread_mutex_unlock(&lock);
			sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));

			pthread_mutex_lock(&lock);
			sprintf(sendmesg, "TEMP=%.1f\r\n", status.temp_f);
			pthread_mutex_unlock(&lock);
			sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));

			pthread_mutex_lock(&lock);
			sprintf(sendmesg, "DISTANCE=%.1f\r\n", status.distance_in);
			pthread_mutex_unlock(&lock);
			sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));

			pthread_mutex_lock(&lock);
			sprintf(sendmesg, "BEEPER=%s\r\n", (status.beeper) ? "on":"off");
			pthread_mutex_unlock(&lock);
			sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));

			sprintf(sendmesg, "HEARTBEAT=true\r\n");
			sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
		}
			
		sleep(control.push_period);
	}
	
	return NULL;
}

void *thread_sensor_sample( void *ptr ) 
{
	float temp_c;
	
	while (!sump_exit)
	{
		// Fetch sensor data
		pthread_mutex_lock(&lock);
		status.distance_in = RangeMeasure(5);
		pthread_mutex_unlock(&lock);
			
		pthread_mutex_lock(&lock);
		dht_read_val(&status.temp_f, &temp_c, &status.humidity_pct);
		pthread_mutex_unlock(&lock);
       
		sleep(control.sensor_period);
	}
	
	return NULL;
}

/*
 *********************************************************************************
 * main
 *********************************************************************************
 */

int  main(void)
{
	pthread_t data_push;
	pthread_t request_handler;
	pthread_t sensor_sample;
	const char *message1 = "thread_data_push";
	const char *message2 = "thread_request_handler";
	const char *message3 = "thread_sensor_sample";
	int  iret1;

	bzero(&servaddr,sizeof(servaddr));
	control.push_period = DEFAULT_PUSH_PERIOD;
	control.sensor_period = DEFAULT_SENSOR_PERIOD;

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
		exit(EXIT_FAILURE);
	}

	iret1 = pthread_create( &data_push, NULL, thread_data_push, (void*) message1);
	if(iret1)
	{
		fprintf(stderr,"Error - pthread_create() return code: %d\n",iret1);
		BeepMorse(5, "Sump Thread Create Fail");
		exit(EXIT_FAILURE);
	}
	else
		printf("Launching thread data_push\r\n");

	iret1 = pthread_create( &request_handler, NULL, thread_request_handler, (void*) message2);
	if(iret1)
	{
		fprintf(stderr,"Error - pthread_create() return code: %d\n",iret1);
		BeepMorse(5, "UDP Thread Create Fail");
		exit(EXIT_FAILURE);
	}
	else
		printf("Launching thread request_handler\r\n");

	iret1 = pthread_create( &sensor_sample, NULL, thread_sensor_sample, (void*) message3);
	if(iret1)
	{
		fprintf(stderr,"Error - pthread_create() return code: %d\n",iret1);
		BeepMorse(5, "thread_sensor_sample Thread Create Fail");
		exit(EXIT_FAILURE);
	}
	else
		printf("Launching thread sensor_sample\r\n");

	while (!sump_exit) sleep(0);

	// Exit	
	pthread_join(data_push, NULL);
	pthread_join(request_handler, NULL);
	pthread_join(sensor_sample, NULL);
	pthread_mutex_destroy(&lock);

	BeepMorse(5, "OK");
	return 0;
}

