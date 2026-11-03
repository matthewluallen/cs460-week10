#ifndef TBD_BRIDGE_H
#define TBD_BRIDGE_H
#include "hw/i2c/i2c.h"
int tbd_twin_weight_raw(void);
void tbd_physics_register(I2CSlave *aw, I2CSlave *ina, I2CSlave *lm, I2CSlave *ioexp);
void tbd_w5500_cs_notify(int cs_low);
#endif
