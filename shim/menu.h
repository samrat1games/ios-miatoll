#pragma once
#include <stdint.h>

/* Boot menu "iMiatoll UEFI". Returns the extra boot-args for iOS (may be ""), or NULL when the user chose Android
 * or Power off (those do not return) -- never NULL for iOS. Keys: volume up/down move, power selects. */
const char *menu_run(void);
