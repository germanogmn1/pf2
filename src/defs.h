#pragma once

#include <stdint.h>

#define ARRAY_LENGTH(a) (sizeof(a) / sizeof((a)[0]))

#define KILOBYTE 0x400
#define MEGABYTE 0x100000
#define GIGABYTE 0x40000000

#define PI 3.1415926535f
#define DEG2RAD (PI/180.0f)
#define RAD2DEG (180.0f/PI)
