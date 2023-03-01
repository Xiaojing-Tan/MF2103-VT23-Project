  /* USER CODE BEGIN WHILE */
  while (1)
  {
    printf("Opening socket... ");
    // Open socket
    if((retval = socket(APP_SOCK, SOCK_STREAM, SERVER_PORT, SF_TCP_NODELAY)) == APP_SOCK)
    {
      printf("Success!\r\n");
#ifdef _SERVER_CONFIG
      // Put socket in listen mode
      printf("Listening... ");
      if((retval = listen(APP_SOCK)) == SOCK_OK)
      {
        printf("Success!\r\n");
        retval = getsockopt(APP_SOCK, SO_STATUS, &sock_status);
        while (sock_status == SOCK_LISTEN || sock_status == SOCK_ESTABLISHED)
        {
          // If the client has connected, print the message
          if (sock_status == SOCK_ESTABLISHED)
          {
            retval = recv(APP_SOCK, (uint8_t*)&msg, sizeof(msg));
            printf("Received: %d\r\n", msg);
          }
          // Otherwise, wait for 100 msec and check again
          else HAL_Delay(100);
          retval = getsockopt(APP_SOCK, SO_STATUS, &sock_status);
        }
#else
      // Try to connect to server
      printf("Connecting to server... ");
      if((retval = connect(APP_SOCK, server_addr, SERVER_PORT)) == SOCK_OK)
      {
        printf("Success!\r\n");
        for (msg = 0; msg < 100; msg++)
        {
          retval = getsockopt(APP_SOCK, SO_STATUS, &sock_status);
          if (sock_status == SOCK_ESTABLISHED)
          {
            retval = send(APP_SOCK, (uint8_t*)&msg, sizeof(msg));
            printf("Sent: %d\r\n", msg);
            HAL_Delay(200);
          }
        }
#endif
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
    HAL_Delay(500);
    /* USER CODE END WHILE */