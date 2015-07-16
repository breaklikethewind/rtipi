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

#include <sys/socket.h>
#include <netinet/in.h>

#include "beep.h"
#include "range.h"
#include "dht_read.h"

#define BeepPin 2 // Raspberry pi gpio27
#define EchoPin 7 // Raspberry pi gpio4
#define TriggerPin 0 // Raspberry pi gpio 17
#define DHTPin 5 // GPIO 24

#define SAMPLE_PERIOD 900 // Seconds

typedef struct
{
	unsigned int humidity_alarm_setpoint;
	unsigned int temp_alarm_setpoint;
	unsigned int distance_alarm_setpoint;
	bool beeper_state;
	char beeper_message[128];
} control_t;

typedef struct
{
	float humidity_pct;
	float temp_f;
	unsigned int distance_in;
	bool beeper;
} status_t;

control_t control;
status_t status;
pthread_mutex_t lock; // sync between UDP thread and main
void *sump_control( void *ptr );
void *udp_control( void *ptr );
bool sump_exit = false;

void *udp_control( void *ptr ) 
{
	int sockfd,n;
	struct sockaddr_in servaddr,cliaddr;
	socklen_t len;
	char mesg[1000];
	char sendmesg[1000] = {0};

	sockfd=socket(AF_INET,SOCK_DGRAM,0);

	bzero(&servaddr,sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
	servaddr.sin_port=htons(32000);
	bind(sockfd,(struct sockaddr *)&servaddr,sizeof(servaddr));

	printf("THREAD UDP CONTROL\r\n");
#error need to create/handle UDP messages
	for (;;)
	{
		len = sizeof(cliaddr);
		n = recvfrom(sockfd,mesg,1000,0,(struct sockaddr *)&cliaddr,&len);
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
			sprintf(sendmesg, "DISTANCE=%d\r\n", status.distance_in);
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
			sump_shutdown = true;          
		}
		else 
		{
			sprintf(sendmesg, "INVALID COMMAND\r\n");
			sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
		}
	}
}

void *sump_control( void *ptr ) 
{
#error need to write sump control (currently using sample)
	float temp_c;
	
	
	for (;;) 
	{ 
		pthread_mutex_lock(&lock);
		status.distance_in = RangeMeasure(5);
		pthread_mutex_unlock(&lock);
			
		pthread_mutex_lock(&lock);
		dht_read_val(&status.temp_f, &temp_c, &status.humidity_pct)
		pthread_mutex_unlock(&lock);
       
		delay(SAMPLE_PERIOD);
	}
}

void printhelp(void)
{
#error need to update for sump.c (currently using sample)
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

int  main(void)
{
	pthread_t sumpControl;
	pthread_t udpControl;
	const char *message1 = "sump_control";
	const char *message2 = "udp_control";
	int  iret1;

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

	iret1 = pthread_create( &sumpControl, NULL, sump_control, (void*) message1);
	if(iret1)
	{
		fprintf(stderr,"Error - pthread_create() return code: %d\n",iret1);
		BeepMorse(5, "Sump Thread Create Fail");
		exit(EXIT_FAILURE);
	}

	iret1 = pthread_create( &udpControl, NULL, udp_control, (void*) message2);
	if(iret1)
	{
		fprintf(stderr,"Error - pthread_create() return code: %d\n",iret1);
		BeepMorse(5, "UDP Thread Create Fail");
		exit(EXIT_FAILURE);
	}

	while (!sump_exit);

	// Exit	
	pthread_join(sumpControl, NULL);
	pthread_join(udpControl, NULL);
	pthread_mutex_destroy(&lock);

	BeepMorse(5, "OK");
	return 0;
}

#error Remove refrence code
#if 0

// The following is just for refrence. Remove when no longer needed

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
#endif