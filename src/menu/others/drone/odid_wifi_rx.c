/*
Copyright (C) 2020 Simon Wunderlich, Marek Sobe
Copyright (C) 2020 Doodle Labs

SPDX-License-Identifier: Apache-2.0

Open Drone ID C Library

Maintainer:
Simon Wunderlich
sw@simonwunderlich.de
*/

#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#else
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#endif

#include <errno.h>
#include <time.h>

#include "opendroneid.h"
#include "odid_wifi.h"

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#if defined(IDF_VER)
#include <endian.h>
#define cpu_to_be16(x)  (bswap16(x))
#define cpu_to_be32(x)  (bswap32(x))
#else
#include <byteswap.h>
#define cpu_to_be16(x)  (bswap_16(x))
#define cpu_to_be32(x)  (bswap_32(x))
#endif
#define cpu_to_le16(x)  (x)
#define cpu_to_le64(x)  (x)
#else
#define cpu_to_be16(x)      (x)
#define cpu_to_be32(x)      (x)
#define cpu_to_le16(x)      (bswap_16(x))
#define cpu_to_le64(x)      (bswap_64(x))
#endif

#define IEEE80211_FCTL_FTYPE          0x000c
#define IEEE80211_FCTL_STYPE          0x00f0

#define IEEE80211_FTYPE_MGMT            0x0000
#define IEEE80211_STYPE_ACTION          0x00D0
#define IEEE80211_STYPE_BEACON          0x0080

/* IEEE 802.11-2016 capability info */
#define IEEE80211_CAPINFO_ESS               0x0001
#define IEEE80211_CAPINFO_IBSS              0x0002
#define IEEE80211_CAPINFO_CF_POLLABLE       0x0004
#define IEEE80211_CAPINFO_CF_POLLREQ        0x0008
#define IEEE80211_CAPINFO_PRIVACY           0x0010
#define IEEE80211_CAPINFO_SHORT_PREAMBLE    0x0020
/* bits 6-7 reserved */
#define IEEE80211_CAPINFO_SPECTRUM_MGMT     0x0100
#define IEEE80211_CAPINFO_QOS               0x0200
#define IEEE80211_CAPINFO_SHORT_SLOTTIME    0x0400
#define IEEE80211_CAPINFO_APSD              0x0800
#define IEEE80211_CAPINFO_RADIOMEAS         0x1000
/* bit 13 reserved */
#define IEEE80211_CAPINFO_DEL_BLOCK_ACK     0x4000
#define IEEE80211_CAPINFO_IMM_BLOCK_ACK     0x8000

/* IEEE 802.11 Element IDs */
#define IEEE80211_ELEMID_SSID		0x00
#define IEEE80211_ELEMID_RATES		0x01
#define IEEE80211_ELEMID_VENDOR		0xDD
int odid_message_process_pack(ODID_UAS_Data *UAS_Data, uint8_t *pack, size_t buflen)
{
    ODID_MessagePack_encoded *msg_pack_enc = (ODID_MessagePack_encoded *) pack;
    size_t size = sizeof(*msg_pack_enc) - ODID_MESSAGE_SIZE * (ODID_PACK_MAX_MESSAGES - msg_pack_enc->MsgPackSize);
    if (size > buflen)
        return -ENOMEM;

    odid_initUasData(UAS_Data);

    if (decodeMessagePack(UAS_Data, msg_pack_enc) != ODID_SUCCESS)
        return -1;

    return (int) size;
}

int odid_wifi_receive_message_pack_nan_action_frame(ODID_UAS_Data *UAS_Data,
                                                    char *mac, uint8_t *buf, size_t buf_size)
{
    struct ieee80211_mgmt *mgmt;
    struct nan_service_discovery *nsd;
    struct nan_service_descriptor_attribute *nsda;
    struct nan_service_descriptor_extension_attribute *nsdea;
    struct ODID_service_info *si;
    uint8_t target_addr[6] = { 0x51, 0x6F, 0x9A, 0x01, 0x00, 0x00 };
    uint8_t wifi_alliance_oui[3] = { 0x50, 0x6F, 0x9A };
    uint8_t service_id[6] = { 0x88, 0x69, 0x19, 0x9D, 0x92, 0x09 };
    int ret;
    size_t len = 0;

    /* IEEE 802.11 Management Header */
    if (len + sizeof(*mgmt) > buf_size)
        return -EINVAL;
    mgmt = (struct ieee80211_mgmt *)(buf + len);
    if ((mgmt->frame_control & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) !=
        cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_ACTION))
        return -EINVAL;
    if (memcmp(mgmt->da, target_addr, sizeof(mgmt->da)) != 0)
        return -EINVAL;
    memcpy(mac, mgmt->sa, sizeof(mgmt->sa));

    len += sizeof(*mgmt);

    /* NAN Service Discovery header */
    if (len + sizeof(*nsd) > buf_size)
        return -EINVAL;
    nsd = (struct nan_service_discovery *)(buf + len);
    if (nsd->category != 0x04)
        return -EINVAL;
    if (nsd->action_code != 0x09)
        return -EINVAL;
    if (memcmp(nsd->oui, wifi_alliance_oui, sizeof(wifi_alliance_oui)) != 0)
        return -EINVAL;
    if (nsd->oui_type != 0x13)
        return -EINVAL;
    len += sizeof(*nsd);

    /* NAN Attribute for Service Descriptor header */
    if (len + sizeof(*nsda) > buf_size)
        return -EINVAL;
    nsda = (struct nan_service_descriptor_attribute *)(buf + len);
    if (nsda->header.attribute_id != 0x3)
        return -EINVAL;
    if (memcmp(nsda->service_id, service_id, sizeof(service_id)) != 0)
        return -EINVAL;
    if (nsda->instance_id != 0x01)
        return -EINVAL;
    if (nsda->service_control != 0x10)
        return -EINVAL;
    len += sizeof(*nsda);

    si = (struct ODID_service_info *)(buf + len);
    ret = odid_message_process_pack(UAS_Data, buf + len + sizeof(*si), buf_size - len - sizeof(*nsdea));
    if (ret < 0)
        return -EINVAL;
    if (nsda->service_info_length != (sizeof(*si) + ret))
        return -EINVAL;
    if (nsda->header.length != (cpu_to_le16(sizeof(*nsda) - sizeof(struct nan_attribute_header) + nsda->service_info_length)))
        return -EINVAL;
    len += sizeof(*si) + ret;

    /* NAN Attribute for Service Descriptor extension header */
    if (len + sizeof(*nsdea) > buf_size)
        return -ENOMEM;
    nsdea = (struct nan_service_descriptor_extension_attribute *)(buf + len);
    if (nsdea->header.attribute_id != 0xE)
        return -EINVAL;
    if (nsdea->header.length != cpu_to_le16(0x0004))
        return -EINVAL;
    if (nsdea->instance_id != 0x01)
        return -EINVAL;
    if (nsdea->control != cpu_to_le16(0x0200))
        return -EINVAL;

    return 0;
}
