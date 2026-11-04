/*
 * Keyboard status over a vendor BLE GATT service.
 *
 * Runs on the split central (left half) only. The host reads or subscribes to
 * the status characteristic; the payload layout is a wire contract shared with
 * the host bar plugin, so keep it in sync with README.md.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/keymap.h>
#include <zmk/ble.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
#include <zmk/events/battery_state_changed.h>
#endif

#include "zmk_status_gatt_version.h"

LOG_MODULE_REGISTER(zmk_status_gatt, CONFIG_ZMK_LOG_LEVEL);

#define STATUS_PROTOCOL_VERSION 1
#define STATUS_PAYLOAD_LEN 10
#define FW_VERSION_MAX_LEN 32
#define BATTERY_UNKNOWN 0xFF

/* Byte 7 flags. */
#define FLAG_PERIPHERAL_CONNECTED BIT(0)
#define FLAG_CAPS_WORD_ACTIVE BIT(1)
#define FLAG_CAPS_WORD_SUPPORTED BIT(2)

/*
 * ZMK v0.3.0 raises no event when caps word toggles (its state is private to
 * behavior_caps_word.c), so this firmware cannot track it: bit1 and bit2 stay 0.
 */
#define CAPS_WORD_TRACKING_SUPPORTED 0

/* The single right half is peripheral slot 0 on the central. */
#define PERIPHERAL_SOURCE 0

#define STATUS_SERVICE_UUID BT_UUID_128_ENCODE(0x7a6f0001, 0x2c1e, 0x4b8a, 0x9d33, 0x5e4c0f1a9b20)
#define STATUS_CHRC_UUID BT_UUID_128_ENCODE(0x7a6f0002, 0x2c1e, 0x4b8a, 0x9d33, 0x5e4c0f1a9b20)
#define FIRMWARE_CHRC_UUID BT_UUID_128_ENCODE(0x7a6f0003, 0x2c1e, 0x4b8a, 0x9d33, 0x5e4c0f1a9b20)

BUILD_ASSERT(sizeof(ZMK_STATUS_GATT_FW_VERSION) - 1 <= FW_VERSION_MAX_LEN,
             "Firmware revision string exceeds 32 bytes");

static const char fw_version[] = ZMK_STATUS_GATT_FW_VERSION;

/* State fed by event listeners and connection callbacks. */
static atomic_t peripheral_conn_count = ATOMIC_INIT(0);
static atomic_t central_battery = ATOMIC_INIT(BATTERY_UNKNOWN);
static atomic_t peripheral_battery = ATOMIC_INIT(BATTERY_UNKNOWN);

/* Notification bookkeeping, only touched from the system work queue. */
static uint8_t last_sent[STATUS_PAYLOAD_LEN];
static bool has_last_sent;
static atomic_t force_notify = ATOMIC_INIT(0);

static void notify_work_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(notify_work, notify_work_cb);

static void schedule_notify(void) {
    /* k_work_schedule leaves an already pending item alone, which coalesces bursts. */
    k_work_schedule(&notify_work, K_MSEC(CONFIG_ZMK_STATUS_GATT_NOTIFY_DELAY_MS));
}

static void build_payload(uint8_t out[STATUS_PAYLOAD_LEN]) {
    uint32_t layer_mask = 0;

    /* Bit i = layer at index i (keymap order), matching byte 1. */
    for (int i = 0; i < MIN(ZMK_KEYMAP_LAYERS_LEN, 32); i++) {
        zmk_keymap_layer_id_t id = zmk_keymap_layer_index_to_id(i);

        if (id != ZMK_KEYMAP_LAYER_ID_INVAL && zmk_keymap_layer_active(id)) {
            layer_mask |= BIT(i);
        }
    }

    int profile = zmk_ble_active_profile_index();
    bool peripheral_connected = atomic_get(&peripheral_conn_count) > 0;
    uint8_t flags = 0;

    if (peripheral_connected) {
        flags |= FLAG_PERIPHERAL_CONNECTED;
    }
    if (CAPS_WORD_TRACKING_SUPPORTED) {
        flags |= FLAG_CAPS_WORD_SUPPORTED;
    }

    out[0] = STATUS_PROTOCOL_VERSION;
    out[1] = zmk_keymap_highest_layer_active();
    sys_put_le32(layer_mask, &out[2]);
    out[6] = profile < 0 ? 0 : (uint8_t)profile;
    out[7] = flags;
    out[8] = (uint8_t)atomic_get(&central_battery);
    out[9] = peripheral_connected ? (uint8_t)atomic_get(&peripheral_battery) : BATTERY_UNKNOWN;
}

static ssize_t read_status(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                           uint16_t len, uint16_t offset) {
    uint8_t payload[STATUS_PAYLOAD_LEN];

    build_payload(payload);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, payload, sizeof(payload));
}

static ssize_t read_firmware(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                             uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, fw_version, strlen(fw_version));
}

static void status_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    ARG_UNUSED(attr);

    if (value == BT_GATT_CCC_NOTIFY) {
        /* Push the current state to the new subscriber even if nothing changed. */
        atomic_set(&force_notify, 1);
        schedule_notify();
    }
}

/* Attribute layout: [0] service, [1] status decl, [2] status value, [3] CCC,
 * [4] firmware decl, [5] firmware value. */
#define STATUS_VALUE_ATTR_IDX 2

BT_GATT_SERVICE_DEFINE(
    zmk_status_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_128(STATUS_SERVICE_UUID)),
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(STATUS_CHRC_UUID),
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_READ_ENCRYPT,
                           read_status, NULL, NULL),
    BT_GATT_CCC(status_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(FIRMWARE_CHRC_UUID), BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ, read_firmware, NULL, NULL), );

static void notify_work_cb(struct k_work *work) {
    ARG_UNUSED(work);
    uint8_t payload[STATUS_PAYLOAD_LEN];
    bool force = atomic_cas(&force_notify, 1, 0);

    build_payload(payload);
    if (!force && has_last_sent && memcmp(payload, last_sent, sizeof(payload)) == 0) {
        return;
    }

    int rc = bt_gatt_notify(NULL, &zmk_status_svc.attrs[STATUS_VALUE_ATTR_IDX], payload,
                            sizeof(payload));

    if (rc == -ENOMEM) {
        /* Out of TX buffers: retry with whatever the state is by then. */
        if (force) {
            atomic_set(&force_notify, 1);
        }
        schedule_notify();
        return;
    }
    if (rc < 0 && rc != -ENOTCONN) {
        LOG_WRN("Status notify failed (%d)", rc);
    }

    /* -ENOTCONN means nobody is subscribed; a new subscriber gets a forced push. */
    memcpy(last_sent, payload, sizeof(payload));
    has_last_sent = true;
}

/*
 * Split link state. ZMK v0.3.0 raises zmk_split_peripheral_status_changed only on
 * the peripheral side, so on the central we watch Zephyr's connection callbacks
 * instead. The left half is BLE central only towards split peripherals (towards
 * hosts it is the peripheral), so a connection where we hold the central role
 * is the right half. ZMK's own split central filters the same way.
 */
static bool is_split_peripheral_conn(struct bt_conn *conn) {
    struct bt_conn_info info;

    return bt_conn_get_info(conn, &info) == 0 && info.role == BT_CONN_ROLE_CENTRAL;
}

static void on_connected(struct bt_conn *conn, uint8_t err) {
    if (err || !is_split_peripheral_conn(conn)) {
        return;
    }

    atomic_inc(&peripheral_conn_count);
    schedule_notify();
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(reason);

    if (!is_split_peripheral_conn(conn)) {
        return;
    }

    if (atomic_dec(&peripheral_conn_count) <= 0) {
        atomic_set(&peripheral_conn_count, 0);
    }
    atomic_set(&peripheral_battery, BATTERY_UNKNOWN);
    schedule_notify();
}

BT_CONN_CB_DEFINE(zmk_status_gatt_conn_callbacks) = {
    .connected = on_connected,
    .disconnected = on_disconnected,
};

static int status_event_listener(const zmk_event_t *eh) {
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    const struct zmk_battery_state_changed *battery_ev = as_zmk_battery_state_changed(eh);

    if (battery_ev != NULL) {
        atomic_set(&central_battery, battery_ev->state_of_charge);
    }

    const struct zmk_peripheral_battery_state_changed *peripheral_battery_ev =
        as_zmk_peripheral_battery_state_changed(eh);

    if (peripheral_battery_ev != NULL) {
        /* ZMK reports level 0 when the peripheral disconnects; ignore that and any
         * reading for a link we do not consider up. */
        if (peripheral_battery_ev->source != PERIPHERAL_SOURCE ||
            atomic_get(&peripheral_conn_count) == 0) {
            return ZMK_EV_EVENT_BUBBLE;
        }
        atomic_set(&peripheral_battery, peripheral_battery_ev->state_of_charge);
    }
#endif

    /* Layer and profile values are read from ZMK when the payload is built. */
    schedule_notify();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(zmk_status_gatt, status_event_listener);
ZMK_SUBSCRIPTION(zmk_status_gatt, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(zmk_status_gatt, zmk_ble_active_profile_changed);
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
ZMK_SUBSCRIPTION(zmk_status_gatt, zmk_battery_state_changed);
ZMK_SUBSCRIPTION(zmk_status_gatt, zmk_peripheral_battery_state_changed);
#endif
