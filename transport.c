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
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>

typedef struct
{
	unsigned int humidity_alarm_setpoint;
	unsigned int temp_alarm_setpoint;
	unsigned int distance_alarm_setpoint;
	bool beeper_state;
	char beeper_message[128];
	int push_period;
	int sensor_period;
	int sump_exit;
	int paired;
	int sequencenumber;
	int manualscan;
} control_t;

typedef struct
{
	float humidity_pct;
	float temp_f;
	float distance_in;
	bool beeper;
} status_t;

commandlist_t commands = { \
{ "GETHUMIDITY", "HUMIDITY", NULL, TYPE_FLOAT, &status.humidity_pct}, \
{ "GETTEMP",     "TEMP",     NULL, TYPE_FLOAT, &status.temp_f}, \
{ NULL,          NULL,       NULL, 0,            NULL} \
};

pushlist_t pushlist = { \
{ "HUMIDITY", TYPE_FLOAT, &status.humidity_pct}, \
{ "TEMP",     TYPE_FLOAT, &status.temp_f}, \
{ NULL,       0,          NULL} \
};

struct sockaddr_in servaddr, cliaddr, alladdr;
control_t control;
status_t status;
pthread_mutex_t lock; // sync between UDP thread and main
void *thread_data_push( void *ptr );
void *thread_request_handler( void *ptr );
void *thread_sensor_sample( void *ptr );
void measure(void);
void data_push(void);

void *thread_request_handler( void *ptr ) 
{
	commandlist_t command_list;
	int sockfd, n;
	socklen_t len;
	char mesg[1000];
	char sendmesg[1000] = {0};
	
	command_list = (commandlist_t)ptr;

	sockfd=socket(AF_INET, SOCK_DGRAM, 0);

	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
	servaddr.sin_port=htons(32000);
	bind(sockfd,(struct sockaddr *)&servaddr,sizeof(servaddr));

	while (!control.sump_exit)
	{
		len = sizeof(cliaddr);
		n = recvfrom(sockfd, mesg, 1000, 0, (struct sockaddr *)&cliaddr, &len);
		printf("-------------------------------------------------------\n");
		mesg[n] = 0;
		printf("Received: %s\r\n",mesg);

		while (command_list->request != NULL)
		{
			if (strncmp(mesg, command_list->request, strlen(command_list->request)) == 0)
			{
			pthread_mutex_lock(&lock);
			switch (data_type)
			{
				case TYPE_INTEGER:
					sprintf(sendmesg, "%s=%u\r\n", command_list->tag, *(int*)command_list->data);
					break;
				case TYPE_FLOAT:
					sprintf(sendmesg, "%s=%.1f\r\n", command_list->tag, *(float*)command_list->data);
					break;
				case TYPE_STRING:
					sprintf(sendmesg, "%s=%s\r\n", command_list->tag, *(char**)command_list->data);
					break;
			}
			pthread_mutex_unlock(&lock);
			sendto(sockfd, sendmesg, sizeof(sendmesg), 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr));				
			}
		}		
		else 
		{
			sprintf(sendmesg, "INVALID COMMAND\r\n");
			sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
		}
		printf("Responded: %s", sendmesg);
		printf("-------------------------------------------------------\n");
	}
	
	return NULL;
}

void data_push( void )
{
	int sockfd;
	char sendmesg[1000] = {0};

	printf("Pushing data...\r\n");
	
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);

	// Send sensor data to host
	pthread_mutex_lock(&lock);
	sprintf(sendmesg, "HUMIDITY=%.1f\r\n", status.humidity_pct);
	pthread_mutex_unlock(&lock);
	sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
	printf("%s", sendmesg);

	pthread_mutex_lock(&lock);
	sprintf(sendmesg, "TEMP=%.1f\r\n", status.temp_f);
	pthread_mutex_unlock(&lock);
	sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
	printf("%s", sendmesg);

	pthread_mutex_lock(&lock);
	sprintf(sendmesg, "DISTANCE=%.1f\r\n", status.distance_in);
	pthread_mutex_unlock(&lock);
	sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
	printf("%s", sendmesg);

	pthread_mutex_lock(&lock);
	sprintf(sendmesg, "BEEPER=%s\r\n", (status.beeper) ? "on":"off");
	pthread_mutex_unlock(&lock);
	sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
	printf("%s", sendmesg);

	sprintf(sendmesg, "SEQUENCENUMBER=%u\r\n", control.sequencenumber);
	sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
	control.sequencenumber++;
	printf("%s", sendmesg);
	
	printf("\r\n");
}


