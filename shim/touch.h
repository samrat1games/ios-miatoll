#pragma once
int touch_init(void);
int touch_poll(int *x, int *y);
void wait_us(unsigned us);
