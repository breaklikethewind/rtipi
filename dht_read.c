


#include <wiringPi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sched.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_TIME 85
#define DTTYPE 22 // AM2302 is the same as DHT22

int dhtpin;

int data_val[5] = {0, 0, 0, 0, 0};

void set_max_priority(void)
{
	struct sched_param sched;
	memset(&sched, 0, sizeof(sched));
	// Use FIFO scheduler with highest priority for the lowest chance of the kernel context switching.
	sched.sched_priority = sched_get_priority_max(SCHED_FIFO);
	sched_setscheduler(0, SCHED_FIFO, &sched);
}

void set_default_priority(void)
{
	struct sched_param sched;
	memset(&sched, 0, sizeof(sched));
	// Go back to default scheduler with default 0 priority.
	sched.sched_priority = 0;
	sched_setscheduler(0, SCHED_OTHER, &sched);
}

static uint8_t sizecvt(const int read)
{
  /* digitalRead() and friends from wiringpi are defined as returning a value
  < 256. However, they are returned as int() types. This is a safety function */

	if (read > 255 || read < 0)
	{
		printf("Invalid data from wiringPi library\n");
		exit(EXIT_FAILURE);
	}
	return (uint8_t)read;
}

int dht_read_val(float* farenheit, float* celsius, float* humidity)
{
	uint8_t laststate = HIGH;
	uint8_t counter = 0;
	uint8_t j = 0, i;

	data_val[0] = data_val[1] = data_val[2] = data_val[3] = data_val[4] = 0;

	set_max_priority();

	// pull pin down for 18 milliseconds
	pinMode(dhtpin, OUTPUT);
	digitalWrite(dhtpin, HIGH);
	delay(10);
	digitalWrite(dhtpin, LOW);
	delay(18);

	// then pull it up for 40 microseconds
	digitalWrite(dhtpin, HIGH);
	delayMicroseconds(40);

	// prepare to read pin
	pinMode(dhtpin, INPUT);

	for (i = 0; i < MAX_TIME; i++)
	{
		counter = 0;
		while (sizecvt(digitalRead(dhtpin)) == laststate)
		{
			counter++;
			delayMicroseconds(1);
			if (counter == 255)
				break;
		}
		laststate = sizecvt(digitalRead(dhtpin));
		if (counter == 255)
			break;

		// top 3 transistions are ignored
		if ( (i >= 4) && (i%2 == 0) )
		{
			data_val[j/8] <<= 1;
			if (counter > 16)
				data_val[j/8] |= 1;
			j++;
		}
	} // verify cheksum and print the verified data

	set_default_priority();

	if ( (j >= 40) &&
		(data_val[4] == ( (data_val[0] + data_val[1] + data_val[2] + data_val[3]) & 0xFF) ) &&
		(data_val[4] != 0)
	   )
	{
		if (DTTYPE == 11)
		{
			*humidity = (float)data_val[0] + ((float)data_val[1] / 10.0f);
			*celsius = (float)data_val[2] + ((float)data_val[2] / 10.0f);
		}
		else // (DTTYPE == 22)
		{
			// Calculate humidity and temp for DHT22 sensor.
			*humidity = ((float)data_val[0] * 256.0f + (float)data_val[1]) / 10.0f;
			*celsius = ( (float)(data_val[2] & 0x7F) * 256.0f + (float)data_val[3]) / 10.0f;
			if (data_val[2] & 0x80)
				*celsius *= -1.0f;
		}
		*farenheit = ((*celsius * 9.0f) / 5.0f) + 32.0f;
		return 0;
	}
	else
		return -1;
}

int dht_init(int pin)
{
	dhtpin = pin;
	
	pinMode(dhtpin, INPUT); // set as high impedance

	return 0;
}


