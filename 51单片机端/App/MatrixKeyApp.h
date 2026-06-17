#ifndef __MATRIXKEYAPP_H__
#define __MATRIXKEYAPP_H__

#include "main.h"

void MatrixKey_Task(void);
void MatrixKeyApp_Init(void);

/* 供 UART 命令查询/修改密码 */
unsigned char MatrixKeyApp_GetPassword(unsigned char *out); /* out 填 6 位数字, 返回 1 */
unsigned char MatrixKeyApp_SetPassword(const unsigned char *in); /* in 6 位数字, 返回 1 成功 */

/* ESP32 通知配对成功 */
void MatrixKeyApp_OnPairOk(void);

/* ESP32 通知手机解锁 — 进入欢迎回家页面 */
void MatrixKeyApp_OnUnlock(void);

#endif
