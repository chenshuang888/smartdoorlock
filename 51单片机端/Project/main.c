#include "main.h"

void main()
{

	UART_Init();
	OLED_Init();
	MatrixKeyApp_Init();

	Scheduler_init();

    while(1)
    {
         Scheduler_run();
    }
}
