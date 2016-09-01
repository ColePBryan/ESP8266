/******************************************************************************
 * Copyright 2016 Nefastor Online (nefastor.com)
 *
 * FileName: user_main.c
 *
 * Description: entry file of user application
 *
 * This version demonstrates the ILI9341 LCD controller library
 *
 * Full article and hardware schematics at :
 *
 * www.nefastor.com/esp8266-ili9341-lcd-library
 *
*******************************************************************************/

#include "esp_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

//#include "lwip/sockets.h"
//#include "lwip/dns.h"
//#include "lwip/netdb.h"
#include "lwip/udp.h"

#include "ILI9341.h"

#include <stdio.h>				// For sprintf

#include "credentials.h"		// WiFi network credentials (WIFI_SSID and WIFI_PASS)
#include "unity.h"

// extern globals for debugging unity.c
struct ip_addr unity_IP;

// global variables I want to expose on the Unity front-end
int	exposed_variable = 0x12345678;
int adc;
int test = -120;

// Setup Unity GUI : centralize in a function that can be passed to MCUnity
void MCUnity_setup_function ()
{
	// Application (firmware) specific GUI setup operations:
	unity_setup_int (&exposed_variable, "exposed_variable", 0, 1000, 0xA5);
	unity_setup_int (&adc, "ADC sample", 0, 1023, 0xA3);
	unity_setup_int (&test, "Negative value test", -300, 300, -2);
}

// Establish connection to Unity application
void task_gui_1(void *pvParameters)
{
	drawString ("Waiting...",0,0,4);

	// wait for connection to be established
	// wait until reception and processing of a broadcast packet from Unity
	// TO DO - FIND A MORE ELEGANT WAY TO DO THIS
	while (unity_not_ready())
		vTaskDelay (1);	// delay is necessary, let other tasks work while waiting

	// at this point, it's possible (and time) to setup the GUI
	unity_setup ();		// equivalent to calling MCUnity_setup_function() directly


	char ip_addr[50];
	int a = unity_IP.addr >> 24;
	int b = (unity_IP.addr >> 16) & 0xFF;
	int c = (unity_IP.addr >> 8) & 0xFF;
	int d = unity_IP.addr & 0xFF;
	sprintf (ip_addr,"%i.%i.%i.%i",d,c,b,a);	// reverse byte order
	drawString (ip_addr,0,0,4);				// shows the host IP


	// task ends.
	while (1)
		vTaskDelay (100);

}

// sample the ADC periodically
void task_adc(void *pvParameters)
{
	while (1)
	{
		adc = system_adc_read();
		vTaskDelay(50);
	}
}



/******************************************************************************
 * FunctionName : user_init
 * Description  : entry of user application, init user function here
 * Parameters   : none
 * Returns      : none
*******************************************************************************/

void user_init(void)
{
	// Go to 160 MHz
	system_update_cpu_freq(160);

	// Connect to WiFi network
	wifi_set_opmode(STATION_MODE);	// need to set opmode before you set config
	struct station_config *config = (struct station_config *)zalloc(sizeof(struct station_config));
	sprintf(config->ssid, WIFI_SSID);
	sprintf(config->password, WIFI_PASS);
	wifi_station_set_config_current(config);
	free(config);

	// Initialize TFT (also takes care of HSPI)
	begin();
	setRotation(0);	// 0-2 : portrait. 1-3 : landscape
	//fillScreen(0xFFFF);	// make the screen white
	fillScreen(0x0000);		// make the screen black

	// Let's try something simple : printing a string to the LCD
	// drawString("Test",0,0,2);	// Font 2 is a small font
	// drawString("Test",0,16,4);	// Font 4 is a medium font
	// drawString("Test",0,60,6);	// Font 6 is a large font
	// There's also a font 7 (7-segment display) which only works for numbers :
	// drawNumber(1234,0,200,7);

	// MCUnity needs to know which firmware function will take care of GUI setup :
	unity_init (MCUnity_setup_function);	// initialize with a pointer to a setup function

    // FreeRTOS task creation : function, name, stack depth, parameter to function, priority, handle
    // for more details read : http://www.freertos.org/a00125.html
    xTaskCreate(task_gui_1, "tsk1", 256, NULL, 2, NULL);
    xTaskCreate(task_adc, "tsk3", 256, NULL, 2, NULL);
}

