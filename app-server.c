#include "main.h"
#include <stdio.h>
#include "application.h"
#include "controller.h"
#include "peripherals.h"
#include "cmsis_os.h"
#include "socket.h"
#ifdef _ETHERNET_ENABLED
#include "wizchip_conf.h"
#endif

#define PERIOD_CTRL 10
#define PERIOD_REF 4000

#define APP_SOCK 0
#define SERVER_PORT 2103
#define CLIENT_PORT 2104

/* Global variables ----------------------------------------------------------*/
int16_t encoder;
int32_t reference, velocity, control;
uint32_t millisec;

uint8_t server_addr[4] = {192, 168, 0, 10};
uint8_t retval;
uint8_t sock_status;
uint8_t msg;

/* Function declaration */
void thread_toggle(void const *argument);
void thread_process(void const *argument);
void callback(void const *param);

/* Define threads */
osThreadId toggle_ID, process_ID, main_ID;
osThreadDef(thread_toggle, osPriorityHigh, 1, 0);
osThreadDef(thread_process, osPriorityNormal, 1, 0);

/* Define Timer */
osTimerId timer_toggle;
osTimerDef(timer_toggle_handle, callback);

/* Functions -----------------------------------------------------------------*/

void thread_toggle(void const *argument)
{
	for (;;)
	{
		// Wait for every 4 sec ...
		osSignalWait(0x02, osWaitForever);
		// Flip the direction of the reference
		reference = -reference;
	}
}

void thread_process(void const *argument)
{
	for (;;)
	{
		osSignalWait(0x03, osWaitForever);
		// If the client has connected, print the message
		if (sock_status == SOCK_ESTABLISHED)
		{
			if ((retval = recv(APP_SOCK, (uint8_t *)&velocity, sizeof(velocity))) == sizeof(velocity))
			{
				printf("Received velocity: %d\r\n", velocity);

				// Get system clock
				millisec = Main_GetTickMillisec();

				// Calculate control signal
				control = Controller_PIController(reference, velocity, millisec);

				// Send the control signal
				if ((retval = send(APP_SOCK, (uint8_t *)&control, sizeof(control))) == sizeof(control))
				{
					printf("Sent control: %d\r\n", control);
					osSignalSet(main_ID, 0x01);
				}
				else
				{
					printf("Fail to send control signal!!! \r\n");
					Controller_Reset();
					osSignalSet(main_ID, 0x01);
				}
			}
			else
				printf("Failed to receove velocity!!! \r\n");
			Controller_Reset();
			osSignalSet(main_ID, 0x01);
		}
		else
		{
			printf("Failed establish! \r\n");
			Controller_Reset();
			osSignalSet(main_ID, 0x01);
		}
	}
}

void callback(void const *param)
{
	switch ((uint32_t)param)
	{
	case 0:
		osSignalSet(toggle_ID, 0x02);
		break;
	}
}

/* Run setup needed for all periodic tasks */
int Application_Setup()
{
	// Reset global variables
	reference = 2000;
	velocity = 0;
	control = 0;
	millisec = 0;

	// Initialise hardware
	Peripheral_GPIO_EnableMotor();

	// Initialise(create) timer
	timer_toggle = osTimerCreate(osTimer(timer_toggle_handle), osTimerPeriodic, (void *)0);

	osKernelInitialize();

	// Initialise(create) threads
	toggle_ID = osThreadCreate(osThread(thread_toggle), NULL);
	process_ID = osThreadCreate(osThread(thread_process), NULL);
	main_ID = osThreadGetId();

	osKernelStart();

	return 0;
}

/* Define what to do in the infinite loop */
void Application_Loop()
{
	printf("Opening socket... ");
	// Open socket
	if ((retval = socket(APP_SOCK, SOCK_STREAM, SERVER_PORT, SF_TCP_NODELAY)) == APP_SOCK)
	{
		printf("Success!\r\n");
		// Put socket in listen mode
		printf("Listening... ");
		if ((retval = listen(APP_SOCK)) == SOCK_OK)
		{
			printf("Success!\r\n");
			// Start timer here
			// osTimerStart(timer_toggle, PERIOD_REF);
			retval = getsockopt(APP_SOCK, SO_STATUS, &sock_status);
			while (sock_status == SOCK_LISTEN || sock_status == SOCK_ESTABLISHED)
			{
				// If the client has connected, print the message
				if (sock_status == SOCK_ESTABLISHED)
				{
					// retval = recv(APP_SOCK, (uint8_t *)&msg, sizeof(msg));
					// printf("Received: %d\r\n", msg);
					  osSignalSet(process_ID, 0x03);
					  // Do nothing
					  osSignalWait(0x01, osWaitForever);
				}
				// Otherwise, wait for 100 msec and check again
				else
				{
					printf("sock_status: %d\r\n", sock_status);
					osDelay(100);
				}
				retval = getsockopt(APP_SOCK, SO_STATUS, &sock_status);
			}
			printf("Disconnected! ");
		}
		else // Something went wrong
		{
			printf("Failed! \r\n");
		}
		// Close the socket and start a connection again
		close(APP_SOCK);
		printf("Socket closed.\r\n");
	}
	else // Can't open the socket. This may mean something is wrong with W5500 configuration
	{
		printf("Failed to open socket!\r\n");
	}
	// Wait 500 msec before opening
	osDelay(500);
}
