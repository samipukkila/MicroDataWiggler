#include "gpio.h"
#include "main.h"

#include <stdbool.h>
#include <stdint.h>

void SystemClock_Config(void);

#define USED __attribute__((used))

typedef struct
{
  int8_t in_int8_t;
  int16_t in_int16_t;
  int32_t in_int32_t;
  uint8_t in_uint8_t;
  uint16_t in_uint16_t;
  uint32_t in_uint32_t;
  float in_float;
  double in_double;
  bool in_bool;
} ExampleStruct;

// Write and read variables
USED volatile int8_t in_int8_t = 0;
USED volatile int16_t in_int16_t = 0;
USED volatile int32_t in_int32_t = 0;
USED volatile int64_t in_int64_t = 0;
USED volatile uint8_t in_uint8_t = 0;
USED volatile uint16_t in_uint16_t = 0;
USED volatile uint32_t in_uint32_t = 0;
USED volatile uint64_t in_uint64_t = 0;
USED volatile float in_float = 0;
USED volatile double in_double = 0;
USED volatile bool in_bool = 0;
USED volatile ExampleStruct in_struct = {0};
USED volatile uint8_t in_arr[8] = {0};

// Counterparts
USED volatile int8_t out_int8_t = 0;
USED volatile int16_t out_int16_t = 0;
USED volatile int32_t out_int32_t = 0;
USED volatile int64_t out_int64_t = 0;
USED volatile uint8_t out_uint8_t = 0;
USED volatile uint16_t out_uint16_t = 0;
USED volatile uint32_t out_uint32_t = 0;
USED volatile uint64_t out_uint64_t = 0;
USED volatile float out_float = 0;
USED volatile double out_double = 0;
USED volatile bool out_bool = 0;
USED volatile ExampleStruct out_struct = {0};
USED volatile uint8_t out_arr[8] = {0};

// Counters
USED volatile int8_t cnt_int8_t = 0;
USED volatile int16_t cnt_int16_t = 0;
USED volatile int32_t cnt_int32_t = 0;
USED volatile int64_t cnt_int64_t = 0;
USED volatile uint8_t cnt_uint8_t = 0;
USED volatile uint16_t cnt_uint16_t = 0;
USED volatile uint32_t cnt_uint32_t = 0;
USED volatile uint64_t cnt_uint64_t = 0;
USED volatile float cnt_float = 0;
USED volatile double cnt_double = 0;

USED volatile bool led_on = false;

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();

  static uint16_t tick_count = 0;

  while (1)
  {
    // Write and read variables
    out_int8_t = in_int8_t;
    out_int16_t = in_int16_t;
    out_int32_t = in_int32_t;
    out_int64_t = in_int64_t;
    out_uint8_t = in_uint8_t;
    out_uint16_t = in_uint16_t;
    out_uint32_t = in_uint32_t;
    out_uint64_t = in_uint64_t;
    out_float = in_float;
    out_double = in_double;
    out_bool = in_bool;
    out_struct = in_struct;
    for (int i = 0; i < 8; i++)
    {
      out_arr[i] = in_arr[i];
    }

    // Increment counters every 100ms (100 ticks at 1ms per iteration)
    tick_count++;
    if (tick_count >= 100)
    {
      tick_count = 0;

      cnt_int8_t++;
      cnt_int16_t++;
      cnt_int32_t++;
      cnt_int64_t++;
      cnt_uint8_t++;
      cnt_uint16_t++;
      cnt_uint32_t++;
      cnt_uint64_t++;
      cnt_float++;
      cnt_double++;
    }

    // LED control from external write
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, led_on ? GPIO_PIN_SET : GPIO_PIN_RESET);

    HAL_Delay(1);
  }
}
