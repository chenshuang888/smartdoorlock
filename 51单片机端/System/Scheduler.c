#include "Scheduler.h"

typedef struct {
    void (*task_func)(void);  
    unsigned long rate_ms;         
    unsigned long last_run;        
} scheduler_task_t;

unsigned char task_num;
unsigned long uwtick;

static scheduler_task_t scheduler_task[] =
{
	{UART_Task,5,0},
	{MatrixKey_Task,10,0},
};

void Timer0_init(void)
{
	// 定时器0初始化 (12MHz晶振，定时1ms)
    TMOD &= 0xF0;
    TMOD |= 0x01; // 方式1
    TH0 = 0xFC;   
    TL0 = 0x18;
    
    ET0 = 1;      // 开启 T0 中断
    EA  = 1;      // 开启总中断
    TR0 = 1;      // 启动定时器
}

void Scheduler_init(void)
{
	Timer0_init();
	task_num=sizeof(scheduler_task) / sizeof(scheduler_task_t);
}

void Scheduler_run(void)
{
	unsigned char i = 0;
    for (i = 0; i < task_num; i++)
    {
        unsigned long now_time = uwtick;

        if (now_time >= scheduler_task[i].rate_ms + scheduler_task[i].last_run)
        {
            scheduler_task[i].last_run = now_time;
            scheduler_task[i].task_func();
        }
    }
}

void timer0_isr(void) interrupt 1
{
    TH0 = 0xFC;   
    TL0 = 0x18;
    
	uwtick++;
}