#include "controller.h"

static int16_t encoder_old = 0;
static int32_t error_integral=0;
int8_t flag = 1;

/* Calculate the current velocity in rpm, based on encoder value and time */
int32_t Controller_CalculateVelocity(int16_t encoder, uint32_t millisec)
{
	// Calculate differences
	static uint32_t millisec_old = 0;
	int32_t dt = millisec - millisec_old;
	int16_t diff_encoder = encoder - encoder_old;

	// Encoder: PPR = 512 * 4 = 2048
	// encoder ++ to be positive direction
	int32_t vel = 60000 * diff_encoder / 2048 / dt;

	// Update olde values
	millisec_old = millisec;
	encoder_old = encoder;

	return vel;
}

/* Apply a PI-control law to calcuate the control signal for the motor*/
int32_t Controller_PIController(int32_t ref, int32_t current, uint32_t millisec)
{
	// Control parameters
	float Kp = 0.2, Ki = 0.01;

	// Calculate differences
	static uint32_t millisec_old = 0;
	int32_t dt = millisec - millisec_old;
	int32_t error = ref - current;

	// Take integral
	error_integral += error * dt;

	// Calculate control signal
	int32_t control = Kp * error + Ki * error_integral;
	int32_t control_sat = control;

	// Trajectory planner
	if (flag)
	{
		control_sat = 500;
		flag = 0;
	}
	
	// update
	millisec_old = millisec;

	return control_sat;
}

/* Reset internal state variables, such as the integrator */
void Controller_Reset(void)
{
	flag = 1;
	return;
}
