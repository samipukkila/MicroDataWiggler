#ifndef STLINK_FAKE_H_
#define STLINK_FAKE_H_

#include <stdint.h>

#include "fff.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _stlink {
  uint32_t chip_id;
  unsigned char q_buf[(1024 * 100)];

} stlink_t;

enum connect_type {
  CONNECT_HOT_PLUG = 0,
  CONNECT_NORMAL = 1,
  CONNECT_UNDER_RESET = 2,
};

enum ugly_loglevel {
  UINFO = 0,
};

DECLARE_FAKE_VALUE_FUNC(uint32_t, TickUs_Micros);
DECLARE_FAKE_VALUE_FUNC(uint32_t, TickUs_MicrosMax);

DECLARE_FAKE_VALUE_FUNC(stlink_t *, stlink_open_usb, enum ugly_loglevel,
                        enum connect_type, char *, int32_t);
DECLARE_FAKE_VALUE_FUNC(int32_t, stlink_enter_swd_mode, stlink_t *);
DECLARE_FAKE_VALUE_FUNC(int32_t, stlink_exit_debug_mode, stlink_t *);
DECLARE_FAKE_VALUE_FUNC(int32_t, stlink_read_mem32, stlink_t *, uint32_t,
                        uint16_t);
DECLARE_FAKE_VALUE_FUNC(int32_t, stlink_write_mem32, stlink_t *, uint32_t,
                        uint16_t);
DECLARE_FAKE_VOID_FUNC(stlink_close, stlink_t *);

#ifdef __cplusplus
}
#endif

#endif /* STLINK_FAKE_H_ */
