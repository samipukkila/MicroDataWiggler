#include "stlink.h"

DEFINE_FAKE_VALUE_FUNC(uint32_t, TickUs_Micros);
DEFINE_FAKE_VALUE_FUNC(uint32_t, TickUs_MicrosMax);

DEFINE_FAKE_VALUE_FUNC(stlink_t *, stlink_open_usb, enum ugly_loglevel,
                       enum connect_type, char *, int32_t);
DEFINE_FAKE_VALUE_FUNC(int32_t, stlink_enter_swd_mode, stlink_t *);
DEFINE_FAKE_VALUE_FUNC(int32_t, stlink_exit_debug_mode, stlink_t *);
DEFINE_FAKE_VALUE_FUNC(int32_t, stlink_read_mem32, stlink_t *, uint32_t,
                       uint16_t);
DEFINE_FAKE_VALUE_FUNC(int32_t, stlink_write_mem32, stlink_t *, uint32_t,
                       uint16_t);
DEFINE_FAKE_VOID_FUNC(stlink_close, stlink_t *);
