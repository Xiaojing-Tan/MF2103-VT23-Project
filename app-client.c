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

/* Defines that needed for self written recv_delay function */
#define CHECK_SOCKNUM()   \
   do{                    \
      if(sn > _WIZCHIP_SOCK_NUM_) return SOCKERR_SOCKNUM;   \
   }while(0);             \

#define CHECK_SOCKMODE(mode)  \
   do{                     \
      if((getSn_MR(sn) & 0x0F) != mode) return SOCKERR_SOCKMODE;  \
   }while(0);              \

#define CHECK_SOCKDATA()   \
   do{                     \
      if(len == 0) return SOCKERR_DATALEN;   \
   }while(0);              \
static uint16_t sock_io_mode = 0;

/* Global variables ----------------------------------------------------------*/
int16_t encoder;
int32_t velocity, control;
uint32_t millisec;
uint8_t flag_recv = 0;

uint8_t server_addr[4] = {192, 168, 0, 10};
uint8_t retval_client;
uint8_t sock_status_client;
uint8_t msg_client;

/* Function declaration */
void thread_getspeed(void const *argument);
void thread_PWM(void const *argument);
void thread_communicate(void const *argument);
void callback(void const *param);
// define a reveive function that forcely quit after 5 miliseconds
int32_t recv_delay(uint8_t sn, uint8_t *buf, uint16_t len);

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
    retval_client = getsockopt(APP_SOCK, SO_STATUS, &sock_status_client);
    if (sock_status_client == SOCK_ESTABLISHED)
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
    if (flag_recv)
    {
      printf("Connection error!!! \r\n");
      Peripheral_PWM_ActuateMotor(0);
      osSignalSet(main_ID, 0x01);
    }
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
    if ((retval_client = send(APP_SOCK, (uint8_t *)&velocity, sizeof(velocity))) == sizeof(velocity))
    {
      printf("Sent velocity: %d. Waiting for control signal... \r\n", velocity);
    }
    else
    {
      printf("Failed to send velocity!!! \r\n");
      osSignalSet(main_ID, 0x01);
    }
    // Actuate PWM unitl receive control signal
    // Reset flag_recv
    flag_recv = 0;
    if ((retval_client = recv_delay(APP_SOCK, (uint8_t *)&control, sizeof(control))) == sizeof(control))
    {
      printf("Received control: %d\r\n", control >> 19);
      osSignalSet(PWM_ID, 0x04);
    }
    else
    {
      printf("Failed to receive control!!! \r\n");
      flag_recv = 1;
      osSignalSet(PWM_ID, 0x04);
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
  if ((retval_client = socket(APP_SOCK, SOCK_STREAM, SERVER_PORT, SF_TCP_NODELAY)) == APP_SOCK)
  {
    printf("Success!\r\n");
    // Try to connect to server
    printf("Connecting to server... ");
    if ((retval_client = connect(APP_SOCK, server_addr, SERVER_PORT)) == SOCK_OK)
    {
      printf("Success!\r\n");
      //			for (msg_client = 0; msg_client < 100; msg_client++)
      //			{
      //				retval_client = getsockopt(APP_SOCK, SO_STATUS, &sock_status_client);
      //				if (sock_status_client == SOCK_ESTABLISHED)
      //				{
      //					retval_client = send(APP_SOCK, (uint8_t *)&msg_client, sizeof(msg_client));
      //					 printf("Sent: %d\r\n", msg_client);
      //					osDelay(200);
      //				}
      //			}
      retval_client = getsockopt(APP_SOCK, SO_STATUS, &sock_status_client);
      while (sock_status_client == SOCK_ESTABLISHED)
      {
        // Do nothing
        // Start timer here
        osTimerStart(timer_ctrl, PERIOD_CTRL);
        osSignalWait(0x01, osWaitForever);
        retval_client = getsockopt(APP_SOCK, SO_STATUS, &sock_status_client);
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

int32_t recv_delay(uint8_t sn, uint8_t *buf, uint16_t len)
{
  uint32_t start_time = Main_GetTickMillisec();
  uint8_t tmp = 0;
  uint16_t recvsize = 0;
  CHECK_SOCKNUM();
  CHECK_SOCKMODE(Sn_MR_TCP);
  CHECK_SOCKDATA();

  recvsize = getSn_RxMAX(sn);
  if (recvsize < len)
    len = recvsize;

  while (1)
  {
    recvsize = getSn_RX_RSR(sn);
    tmp = getSn_SR(sn);
    if (tmp != SOCK_ESTABLISHED)
    {
      if (tmp == SOCK_CLOSE_WAIT)
      {
        if (recvsize != 0)
          break;
        else if (getSn_TX_FSR(sn) == getSn_TxMAX(sn))
        {
          close(sn);
          return SOCKERR_SOCKSTATUS;
        }
      }
      else
      {
        close(sn);
        return SOCKERR_SOCKSTATUS;
      }
    }
    if ((sock_io_mode & (1 << sn)) && (recvsize == 0))
      return SOCK_BUSY;
    if (recvsize != 0)
      break;
    if (Main_GetTickMillisec() > (start_time + 5))
    {
      flag_recv = 1;
      break;
    }
  };

  if (recvsize < len)
    len = recvsize;
  wiz_recv_data(sn, buf, len);
  setSn_CR(sn, Sn_CR_RECV);
  while (getSn_CR(sn))
  {
    if (Main_GetTickMillisec() > (start_time + 5))
    {
      flag_recv = 1;
      break;
    }
  }
  return (int32_t)len;
}
