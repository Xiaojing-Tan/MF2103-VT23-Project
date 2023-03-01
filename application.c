#include "main.h"

#include "application.h"
#include "controller.h"
#include "peripherals.h"
#include "cmsis_os.h"

#define PERIOD_CTRL 10
#define PERIOD_REF 4000

/* Global variables ----------------------------------------------------------*/
int16_t encoder;
int32_t reference, velocity, control;
uint32_t millisec;

/* Define threads */
void thread_control(void const *argument);
void thread_toggle(void const *argument);
osThreadId control_ID;
osThreadId toggle_ID;
osThreadDef(thread_control, osPriorityAboveNormal, 1, 0);
osThreadDef(thread_toggle, osPriorityNormal, 1, 0);
/* Threads creating function */
void threads_create()
{
	control_ID = osThreadCreate(osThread(thread_control), NULL);
	toggle_ID = osThreadCreate(osThread(thread_toggle), NULL);
}

/* Define virtual timers */
void callback(void const *param);
osTimerId timer_ctrl;
osTimerId timer_togl;
osTimerDef(timer_ctrl_handle, callback);
osTimerDef(timer_togl_handle, callback);
/* Virtual timers creating function */
void timers_create()
{
	timer_ctrl = osTimerCreate(osTimer(timer_ctrl_handle), osTimerPeriodic, (void *)0);
	timer_togl = osTimerCreate(osTimer(timer_togl_handle), osTimerPeriodic, (void *)1);
}
/* Start timers */
void timers_start()
{
	osTimerStart(timer_ctrl, PERIOD_CTRL);
	osTimerStart(timer_togl, PERIOD_REF);
}

/* Functions -----------------------------------------------------------------*/

/* Sample the encoder, calculate the control signal, and apply it */
void thread_control(void const *argument)
{
	for (;;)
	{
		osSignalWait(0x02, osWaitForever);

		// Get system clock
		millisec = Main_GetTickMillisec();

		// Get current velocity
		encoder = Peripheral_Timer_ReadEncoder();
		velocity = Controller_CalculateVelocity(encoder, millisec);

		// Calculate control signal
		control = Controller_PIController(reference, velocity, millisec);

		// Apply control signal to motor
		Peripheral_PWM_ActuateMotor(control);
	}
}

/* Toggle the direction of the reference */
void thread_toggle(void const *argument)
{
	for (;;)
	{
		// Wait for every 4 sec ...
		osSignalWait(0x03, osWaitForever);
		// Flip the direction of the reference
		reference = -reference;
	}
}

void callback(void const *param)
{
	switch ((uint32_t)param)
	{
	case 0:
		osSignalSet(control_ID, 0x02);
		break;
	case 1:
		osSignalSet(toggle_ID, 0x03);
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

	return 0;
}

/* Define what to do in the infinite loop */
void Application_Loop()
{
	// Create virtual timers
	timers_create();

	osKernelInitialize();
	
	// Start virtual timers
	timers_start();

	// Create threads
	threads_create();

	osKernelStart();

	// Do nothing
	osSignalWait(0x01, osWaitForever);
}
