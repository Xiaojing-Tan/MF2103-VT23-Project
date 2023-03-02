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
int32_t velocity, control;
uint32_t millisec;

uint8_t server_addr[4] = {192, 168, 0, 10};
uint8_t retval;
uint8_t sock_status;
uint8_t msg;

/* Function declaration */
void thread_getspeed(void const *argument);
void thread_PWM(void const *argument);
void thread_communicate(void const *argument);
void callback(void const *param);

/* Define threads */
osThreadId getspeed_ID, PWM_ID, communicate_ID, main_ID;
osThreadDef(thread_getspeed, osPriorityNormal, 1, 0);
osThreadDef(thread_PWM, osPriorityNormal, 1, 0);
osThreadDef(thread_communicate, osPriorityNormal, 1, 0);

/* Define Timer */
osTimerId timer_ctrl;
osTimerDef(timer_ctrl_handle, callback);

/* Functions -----------------------------------------------------------------*/

/* Read encoder and calculate the velocity */
void thread_getspeed(void const *argument)
{
	for (;;)
	{
		osSignalWait(0x02, osWaitForever);
		// Get system clock
		millisec = Main_GetTickMillisec();

		// Get current velocity
		encoder = Peripheral_Timer_ReadEncoder();
		velocity = Controller_CalculateVelocity(encoder, millisec);
		printf("Get velocity: %d\r\n", velocity);

		// Check connection
		retval = getsockopt(APP_SOCK, SO_STATUS, &sock_status);
		if (sock_status == SOCK_ESTABLISHED)
		{
			// Start communicating thread
			osSignalSet(communicate_ID, 0x03);
		}
		else
		{
			// Stop the motor
			Peripheral_PWM_ActuateMotor(0);
			printf("Disconnected! Motor stopped! ");
			osSignalSet(main_ID, 0x01);
		}
	}
}

void thread_PWM(void const *argument)
{
	for (;;)
	{
		osSignalWait(0x04, osWaitForever);
		// Apply control signal to motor
		printf("Acuate control: %d\r\n", control);
		Peripheral_PWM_ActuateMotor(control);
		osSignalSet(main_ID, 0x01);
	}
}

void thread_communicate(void const *argument)
{
	for (;;)
	{
		osSignalWait(0x03, osWaitForever);
		// Send the velocity
		if ((retval = send(APP_SOCK, (uint8_t *)&velocity, sizeof(velocity))) == sizeof(velocity))
		{
			printf("Sent velocity: %d. Waiting for control signal... \r\n", velocity);
		}
		else
		{
			printf("Failed to send velocity!!! \r\n");
			osSignalSet(main_ID, 0x01);
		}
		// Actuate PWM unitl receive control signal
		if ((retval = recv(APP_SOCK, (uint8_t *)&control, sizeof(control))) == sizeof(control))
		{
			printf("Received control: %d\r\n", control);
			osSignalSet(PWM_ID, 0x04);
		}
		else
		{
			printf("Failed to receive control!!! \r\n");
			osSignalSet(main_ID, 0x01);
		}
	}
}

void callback(void const *param)
{
	switch ((uint32_t)param)
	{
	case 0: // 10 ms
		osSignalSet(getspeed_ID, 0x02);
		break;
	}
}

/* Run setup needed for all periodic tasks */
int Application_Setup()
{
	// Reset global variables
	velocity = 0;
	control = 0;
	millisec = 0;

	// Initialise hardware
	Peripheral_GPIO_EnableMotor();

	// Initialise(create) timer
	timer_ctrl = osTimerCreate(osTimer(timer_ctrl_handle), osTimerPeriodic, (void *)0);

	osKernelInitialize();

	// Initialise(create) threads
	PWM_ID = osThreadCreate(osThread(thread_PWM), NULL);
	getspeed_ID = osThreadCreate(osThread(thread_getspeed), NULL);
	communicate_ID = osThreadCreate(osThread(thread_communicate), NULL);
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
		// Try to connect to server
		printf("Connecting to server... ");
		if ((retval = connect(APP_SOCK, server_addr, SERVER_PORT)) == SOCK_OK)
		{
			printf("Success!\r\n");
			// Start timer here
			osTimerStart(timer_ctrl, PERIOD_CTRL);
			// for (msg = 0; msg < 100; msg++)
			// {
			// 	retval = getsockopt(APP_SOCK, SO_STATUS, &sock_status);
			// 	if (sock_status == SOCK_ESTABLISHED)
			// 	{
			// 		retval = send(APP_SOCK, (uint8_t *)&msg, sizeof(msg));
			// 		printf("Sent: %d\r\n", msg);
			// 		osDelay(200);
			// 	}
			// }
			retval = getsockopt(APP_SOCK, SO_STATUS, &sock_status);
			while (sock_status == SOCK_ESTABLISHED)
			{
				// Do nothing
				osSignalWait(0x01, osWaitForever);
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
