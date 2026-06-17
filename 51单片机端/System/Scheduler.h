#ifndef __SCHEDULER_H__
#define __SCHEDULER_H__

#include "main.h"

extern unsigned long uwtick;

void Scheduler_init(void);
void Scheduler_run(void);

#endif
