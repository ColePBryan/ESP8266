

#include "esp_common.h"		// Espressif register names, etc...
#include "gpio.h"			// Nefastor's GPIO library
#include "dht22.h"			// Nefastor's DHT22 library

// output from the sensor in decimal form
int sample_rh = 0;		// to do : change to uint16_t
int sample_t = 0;		// to do : change to uint16_t and interpret sign bit
int sample_valid = 0;	// value is 1 if the current sample is valid (checksum pass)

// raw binary storage for a sensor read (40 bits)
unsigned char samples[5];	// first data bit is in MSB of samples[0]

// debug : duration of each sensor bit's low and high state, including start bit
// these durations are expressed in "calls to os_delay_us(1)", which should be 1 �s
int bit_duration[82];		// even index for low state, odd index for high state
int bit_duration_up[41];	// duration of low state for start bit and data bits
int bit_duration_down[41];	// duration of high state for start bit and data bits
unsigned char bit_state[41];// state of each bit based on their relative duration

// decode sample[] into integer values (stored into globals)
void dht22_sample_decoding()
{
	sample_rh = (samples[0] << 8) + samples[1];
	sample_t = (samples[2] << 8) + samples[3];

/*	uint16_t chksum = samples[0];
	chksum += samples[1];
	chksum += samples[2];
	chksum += samples[3];
	chksum &= 0x00FF;		// keep only the 8 LSB
	*/

	//uint16_t chksum = (samples[0] + samples[1] + samples[2] + samples[3]) & 0x00FF;
	// chksum &= 0x00FF;		// keep only the 8 LSB

	// seems to work
	unsigned char chksum = (uint16_t)(samples[0] + samples[1] + samples[2] + samples[3]) & 0x00FF;

	// if ((samples[0]+samples[1]+samples[2]+samples[3]) == samples[4])
	// for some reason, this doesn't work. I suspect compiler optimizations
/*
	if (chksum == (uint16_t) samples[4])
		sample_valid = 1;
	else
		sample_valid = 0;
	*/

	sample_valid = (chksum == samples[4]) ? 1 : 0;
}

// call this after a read to turn intervals into usable data
void dht22_sample_decoding_thresh()
{
	int ix, bits;
	int threshold = bit_duration[1] >> 1;	// Start bit duration divided by 2.
	// start bit is usually measured as 51-53, low bit at 16-17, high bit as 47-48

	// decoding the samples
	unsigned char b;
	int byte_cnt;

	ix = 3;		// looking at high state durations only, skip start bit
	for (byte_cnt = 0; byte_cnt < 5; byte_cnt++)
	{
		samples[byte_cnt] = 0;
		for (bits = 0, b = 0x80; bits < 8; bits++ , ix += 2)
		{
			if (bit_duration[ix] > 30)
				samples[byte_cnt] |= b;

			b = b >> 1;
		}
	}

	dht22_sample_decoding();
	/*
	sample_rh = (samples[0] << 8) + samples[1];
	sample_t = (samples[2] << 8) + samples[3];

	uint16_t chksum = samples[0];
	chksum += samples[1];
	chksum += samples[2];
	chksum += samples[3];
	chksum &= 0x00FF;

	// if (samples[0]+samples[1]+samples[2]+samples[3] == samples[4])
	if (chksum == (uint16_t) samples[4])
		sample_valid = 1;
	else
		sample_valid = 0;
	*/
}

/* The following function is a dumb implementation, designed to verify sensor
 * operation prior to optimizing the code for multitasking support.
 *
 * Note : this function should not be called within the first second after powering
 * up the sensor.
 */

void dht22_read (void)
{
	// given this function's long duration, feed the watchdog first
	system_soft_wdt_feed();

	int cnt = 0;		// bit counter
	int i = 0;			// bit duration counter (in microseconds)

	for (i=0;i<82;i++) bit_duration[i] = 0;	// clear previous results

	// Start sequence
	// 250ms of high
	GPIO_OUTPUT_SET(DHT_PIN, 1);
	os_delay_us (50000);
	os_delay_us (50000);
	os_delay_us (50000);
	os_delay_us (50000);
	os_delay_us (50000);
	// vTaskDelay (1);		// Also works
	// Hold low for 20ms
	GPIO_OUTPUT_SET(DHT_PIN, 0);
	os_delay_us (20000);
	// vTaskDelay (1);		// Also works
	// High for 40us
	GPIO_OUTPUT_SET(DHT_PIN, 1);
	os_delay_us(40);
	// Set DHT_PIN pin as an input
	GPIO_DIS_OUTPUT(DHT_PIN);

	// at this point, the sensor should immediately pull down the line, and keep
	// it down for 80 us. Let's see if it does.

	// wait for pin to drop (loop should exit immediately)
	// tests show this works (duration : 0)
	while (GPIO_INPUT_GET(DHT_PIN) == 1 && bit_duration[0] < DHT_TIMEOUT)
	{
		os_delay_us(1);
		bit_duration[0]++;
	}

	if (bit_duration[0] == DHT_TIMEOUT) //	return;	// The sensor failed to respond, previous samples remain
	{
		bit_duration[99] = 777;		// arbitrary error code
		return;
	}

	// now count all long the pin stays low
	// should be 80 �s but gets reported as 40.
	// note : re-using bit_duration[0] since I'm storing low time, then high time
	while (GPIO_INPUT_GET(DHT_PIN) == 0 && bit_duration[0] < DHT_TIMEOUT)
	{
		os_delay_us(1);
		bit_duration[0]++;
	}

	// now measure the high period for the start bit
	while (GPIO_INPUT_GET(DHT_PIN) == 1 && bit_duration[1] < DHT_TIMEOUT)
	{
		os_delay_us(1);
		bit_duration[1]++;
	}

	// Now I should read 40 bits in similar fashion. Let's make a loop:
	int bits;
	int ix = 2;
	for (bits = 1; bits<41; bits++)
	{
		// measure low state
		while (GPIO_INPUT_GET(DHT_PIN) == 0)
		{
			os_delay_us(1);
			bit_duration[ix]++;
		}
		ix++;
		// measure high state
		while (GPIO_INPUT_GET(DHT_PIN) == 1)
		{
			os_delay_us(1);
			bit_duration[ix]++;
		}
		ix++;
	}

	dht22_sample_decoding();
}

/* "Partially event-driven" version
 *
 * "start function" returns non-zero if busy. The exact value is the number of bits
 * that still remain to be read.
 */

// globals
int bit_index = -1;

void dht22_read_ed (void)
{
	if (bit_index != -1)		// this value is used to indicate no read is in progress
		return;

	bit_index = 0;				// ammounts to indicating that a read is in progress

	// clear previous results
	samples[0] = samples[1] = samples[2] = samples[3] = samples[4] = 0;

	ETS_GPIO_INTR_DISABLE();	// so as not to self-interrupt while sending a read command

	// Send a read command :
	// 250ms of high
	GPIO_OUTPUT_SET(DHT_PIN, 1);
	vTaskDelay (25);		// 1 tick is 10 ms according to port comments
	// Hold low for 20ms
	GPIO_OUTPUT_SET(DHT_PIN, 0);
	vTaskDelay (2);
	// High for 40us
	GPIO_OUTPUT_SET(DHT_PIN, 1);
	os_delay_us(40);
	// Set DHT_PIN pin as an input
	GPIO_DIS_OUTPUT(DHT_PIN);

	// Enable GPIO interrupt
	ETS_GPIO_INTR_ENABLE();

	return;	// and we're done.
}

void dht22_ISR (uint32 mask, void* argument)
{
	// this ISR assumes there's no other GPIO interrupt source than the DHT22
	// it will trigger on I/O pin rising and measure how long it stays high, in �s.

	// this FreeRTOS port doesn't have a ISR-specific macro, not sure this works :
	//portENTER_CRITICAL();   	// Actually they seem to be causing hang-ups !

	static int theshold = 0; // doesn't seem to work
	int bit_timer = 0;

	if (GPIO_INPUT_GET(DHT_PIN) == 0)		// glitch protection
	{
		// "rearm" the interrupt (not sure this is actually necessary...)
		uint32_t gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);
		GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status );
		return;
	}

	// Time until pin goes low
	while (GPIO_INPUT_GET(DHT_PIN) == 1)
	{
		os_delay_us(1);
		bit_timer++;
	}

	if (bit_index == 0)		// then we just measured the start bit
	{
		theshold = bit_timer >> 1;	// anything longer than that is a "one"
	}
	else
	{
		// got a data bit : decode and store
		// sample byte index for this bit :
		int samplebyte = (bit_index - 1) >> 3; // each byte holds 8 bits
		// "- 1" to get rid of start bit and get in the 0..39 range
		// which bit in the byte ? we receive MSB to LSB
		int bitinbyte = 7 - ((bit_index - 1) & 0x07);
		// mask for that bit
		unsigned char bitmsk = 1 << bitinbyte;
		// is the bit a one ? (longer than threshold ?)
		if (bit_timer > theshold)
			samples[samplebyte] |= bitmsk;

		// Also store the timing data for debugging
		int dur_idx = (bit_index << 1) + 1;   // index of the duration for this bit
		bit_duration[dur_idx] = bit_timer;

		// Also store the bits as integers in the timing array, for tests
		if (bit_timer > theshold)
			bit_duration[dur_idx - 1] = 1;
		else
			bit_duration[dur_idx - 1] = 0;

	}

	// Detect read completion
	if (bit_index == 40)
	{
		bit_index = -1;		// indicates no read is in progress

		ETS_GPIO_INTR_DISABLE();	// also disable GPIO interrupts

		dht22_sample_decoding();	// call function to decode the results
	}
	else
	{
		bit_index++;	// update index

		// "rearm" the interrupt (not sure this is actually necessary...)
		uint32_t gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);
		GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status );
	}

	 // this port doesn't have a ISR-specific macro, not sure this works :
	 //portEXIT_CRITICAL();
}

// returns 1 until a complete sample has been read
int dht22_read_ed_busy (void)
{
	return (bit_index == -1) ? 0 : 1;
}

void dht22_init (void)
{
	// setup the I/O pin : enable pull-up and rising-edge interrupt
	PIN_PULLUP_EN (GPIO_PIN_REG (DHT_PIN));
	gpio_pin_intr_state_set (DHT_PIN, GPIO_PIN_INTR_POSEDGE);

	// Set the GPIO ISR (note : interrupt enable is handled in dht22_read_ed function)
	gpio_intr_handler_register(dht22_ISR, NULL);
}
