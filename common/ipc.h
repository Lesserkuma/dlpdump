#pragma once
#include "types.h"
#include "protocol.h"
#include "handover.h"
#include "hash_common.h"

#include <string.h>

#define IPC_MAGIC 0x44504c59u /* DPLY */
#define PXI_CMD_KICK 0x03fdf00u
#define EVENT_RING_COUNT 512
#define EVENT_DATA_MAX   1024

typedef enum Arm7CommandId {
    ARM7_CMD_NONE = 0,
    ARM7_CMD_WIFI_INIT = 1,
    ARM7_CMD_SCAN_START = 2,
    ARM7_CMD_SCAN_STOP = 3,
    ARM7_CMD_CONNECT = 4,
    ARM7_CMD_QUEUE_REPLY = 5,
    ARM7_CMD_RESET_TO_SCAN = 6,
    ARM7_CMD_RAW_CAPTURE = 7,
    ARM7_CMD_BOOT_PREPARE = 8,
    ARM7_CMD_BOOT = 9,
    ARM7_CMD_CHILD_CANCEL = 10,
    ARM7_CMD_WIFI_SHUTDOWN = 11,
} Arm7CommandId;

typedef enum Arm7EventType {
    EVENT_NONE = 0,
    EVENT_ARM7_READY = 1,
    EVENT_LOG = 2,
    EVENT_SCAN_BSS = 3,
    EVENT_CONNECTED = 4,
    EVENT_CONNECT_FAILED = 5,
    EVENT_DISCONNECTED = 6,
    EVENT_HOST_CMD = 7,
    EVENT_RAW_FRAME = 8,
    EVENT_ERROR = 9,
    EVENT_BOOT_READY = 10,
    EVENT_SCAN_CHANNEL = 11,
} Arm7EventType;

#define BOOT_FIXED_BEACON_ADDR        HANDOVER_FIXED_BEACON_ADDR
#define BOOT_FIXED_CONTROL_ADDR       HANDOVER_FIXED_CONTROL_ADDR
#define BOOT_ARM7_STATUS_ADDR         HANDOVER_ARM7_STATUS_ADDR
#define BOOT_ARM7_STUB_ADDR           HANDOVER_ARM7_STUB_ADDR
#define BOOT_ARM7_STATUS_COPIED       HANDOVER_ARM7_STATUS_COPIED
#define BOOT_ARM9_NTR_SWITCH          HANDOVER_ARM9_NTR_SWITCH
#define BOOT_ARM7_STATUS_NTR_READY    HANDOVER_ARM7_STATUS_NTR_READY
#define BOOT_ARM7_STATUS_LAUNCH       HANDOVER_ARM7_STATUS_LAUNCH
#define BOOT_ARM9_RELEASE             HANDOVER_ARM9_RELEASE

#define RAW_RX 0
#define RAW_TX 1
#define RAW_DIR_MASK       0x0001
#define RAW_RSSI_SHIFT     1
#define RAW_RSSI_MASK      0x007e
#define RAW_SIGNAL_VALID   0x0080
#define RAW_SIGNAL_SHIFT   8
#define RAW_SIGNAL_MASK    0xff00

#define CONNECT_FAIL_JOIN_START      0x0000u
#define CONNECT_FAIL_JOIN_TIMEOUT    0x0001u
#define CONNECT_FAIL_AUTH_START      0x0002u
#define CONNECT_FAIL_ASSOC_START     0x0003u
#define CONNECT_FAIL_STATUS_MASK     0x00ffu
#define CONNECT_FAIL_PHASE_MASK      0xff00u
#define CONNECT_FAIL_AUTH_STATUS     0x0100u
#define CONNECT_FAIL_ASSOC_STATUS    0x0200u

/**
 * @brief Cache-line-aligned command slot written by ARM9 and consumed by ARM7.
 */
typedef struct ALIGNED_ATTR(32) {
    volatile u32 seq;
    volatile u32 id;
    union {
        ConnectParams connect;
        ReplyParams reply;
        struct { u32 enabled; } raw_capture;
        struct { u32 switch_to_ntr; u32 game_code; } boot_prepare;
    } arg;
} Arm7Command;

/**
 * @brief One cache-line-aligned ARM7-to-ARM9 event ring entry.
 */
typedef struct ALIGNED_ATTR(32) {
    volatile u32 seq;
    volatile u16 type;
    volatile u16 flags;
    volatile u32 timestamp_us;
    volatile u32 event_id;
    volatile u32 data_hash;
    volatile u32 length;
    volatile u32 _pad0;
    volatile u32 _pad1;
    u8 data[EVENT_DATA_MAX];
} Arm7Event;

/**
 * @brief Shared IPC memory block containing command state and the event ring.
 */
typedef struct ALIGNED_ATTR(32) {
    volatile u32 magic;
    u8 _pad_magic_line[28];

    volatile u32 event_w;
    volatile u32 dropped_events;
    u8 _pad_arm7_line[24];

    volatile u32 event_r;
    u8 _pad_arm9_line[28];

    Arm7Command command;
    Arm7Event events[EVENT_RING_COUNT];
} IpcShared;

/**
 * @brief Copies an ARM7 text event into a caller buffer with guaranteed NUL termination.
 *
 * The event data is treated as an untrusted bounded byte span. The copied
 * length is clamped to both `EVENT_DATA_MAX` and `buf_size - 1`, so callers
 * can safely pass the result to `%s` even when ARM7 omitted a terminator.
 */
static inline const char *ipc_event_text(const Arm7Event *evt, char *buf, size_t buf_size) {
    if (!buf || !buf_size) return "";
    buf[0] = 0;
    if (!evt) return buf;
    size_t n = evt->length;
    if (n > EVENT_DATA_MAX) n = EVENT_DATA_MAX;
    if (n >= buf_size) n = buf_size - 1u;
    if (n) memcpy(buf, evt->data, n);
    buf[n] = 0;
    return buf;
}

/** @brief Clamps an outgoing event data length and drops NULL spans. */
static inline u32 ipc_event_data_length(const void *data, u32 len) {
    if (!data) return 0;
    return len > EVENT_DATA_MAX ? EVENT_DATA_MAX : len;
}

/** @brief Computes a bounded NUL-including length for ARM7 text events. */
static inline u32 ipc_text_event_length(const char *s) {
    if (!s) return 0;
    u32 len = 0;
    while (len < EVENT_DATA_MAX && s[len]) len++;
    return len < EVENT_DATA_MAX ? len + 1u : EVENT_DATA_MAX;
}

#ifdef ARM9
#include <nds.h>

/** @brief Flushes a shared IPC range from ARM9 cache before ARM7 reads it. */
static inline void ipc_flush(const void *ptr, size_t len) {
    DC_FlushRange(ptr, len);
}

/** @brief Invalidates a shared IPC range before ARM9 reads ARM7-written bytes. */
static inline void ipc_invalidate(const void *ptr, size_t len) {
    DC_InvalidateRange((void*)ptr, len);
}
#else

/** @brief Host/ARM7 no-op counterpart for the ARM9 cache flush helper. */
static inline void ipc_flush(const void *ptr, size_t len) {
    (void)ptr;
    (void)len;
}

/** @brief Host/ARM7 no-op counterpart for the ARM9 cache invalidate helper. */
static inline void ipc_invalidate(const void *ptr, size_t len) {
    (void)ptr;
    (void)len;
}
#endif

/** @brief Computes the FNV-1a hash used to detect torn IPC host events. */
static inline u32 ipc_hash_bytes(const void *data, u32 len) {
    return fnv1a32(data, len);
}
