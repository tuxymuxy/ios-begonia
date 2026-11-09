/*
 * Begonia (MT6785): Apple80211 IOCTL & Station Interface.
 */
#ifndef _BEGONIA_WLAN_H_
#define _BEGONIA_WLAN_H_

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/kpi_mbuf.h>

#define VWL_IE_MAX      512

struct vwl_bss {                        /* one BSS from the last scans */
	uint8_t  bssid[6], ssid[32], ssid_len, ch;
	int8_t   rssi;
	uint8_t  rsvd;
	uint16_t capinfo, bintval;
	uint16_t ie_len;
	uint8_t  ie[VWL_IE_MAX];        /* beacon / probe response body after the fixed fields */
	uint32_t age_ms;
};

struct vwl_scan {
	uint8_t  ssid[32], ssid_len;    /* 0 = wildcard */
	uint8_t  passive;
	uint8_t  nch;                   /* 0 = all */
	uint8_t  ch[64];
};

struct vwl_join {
	uint8_t  ssid[32], ssid_len;
	uint8_t  bssid[6];              /* zero: the strongest BSS with that SSID */
	uint8_t  has_pmk;               /* WPA2-PSK; otherwise open */
	uint8_t  pmk[32];
	uint8_t  rsnie[64], rsnie_len;  /* RSN IE for the association request (0: built by the driver) */
};

struct vwl_status {
	uint8_t  power;                 /* radio on */
	uint8_t  state;                 /* APPLE80211_S_*: 0 init, 1 scan, 2 auth, 3 assoc, 4 run */
	uint8_t  link;                  /* associated and keys installed: data flows */
	uint8_t  result;                /* APPLE80211 ASSOCIATE_RESULT of the last join (0 = running) */
	uint8_t  ssid[32], ssid_len, ch;
	uint8_t  bssid[6], mac[6];
	uint8_t  rsn;                   /* the current BSS uses WPA2-PSK */
	int8_t   rssi;
	uint16_t rate;                  /* Mbit/s */
	uint32_t txFrames, rxFrames, txDrops;
};

#ifdef __cplusplus
extern "C" {
#endif

/* begonia_wifi.c (station) */
int  begonia_wifi_set_power(bool on);                      /* blocks until the radio is up (or down) */
int  begonia_wifi_scan(const struct vwl_scan *s);          /* async: begonia_wlan_scan_done() */
int  begonia_wifi_scan_result(uint32_t i, struct vwl_bss *out);    /* ENOENT past the end */
int  begonia_wifi_current_bss(struct vwl_bss *out);
int  begonia_wifi_join(const struct vwl_join *j);          /* async: begonia_wlan_assoc_done() */
void begonia_wifi_leave(void);
void begonia_wifi_get_status(struct vwl_status *s);
void begonia_wifi_output(mbuf_t m);                        /* an 802.3 frame; the mbuf is consumed */
void begonia_wifi_mac(uint8_t mac[6]);
void begonia_wifi_link(const char *args, char *out, size_t len);

/* begonia_wlan.c (Apple80211 / BSD side), called by the station without its locks held */
void begonia_wlan_init(void);
void begonia_wlan_attach(void);                            /* en0 */
void begonia_wlan_mac(uint8_t mac[6]);
void begonia_wlan_scan_done(int status);
void begonia_wlan_assoc_done(int result);                  /* APPLE80211 ASSOCIATE_RESULT, 1 = success */
void begonia_wlan_link(bool up, uint32_t reason);
void begonia_wlan_power(bool on);
void begonia_wlan_input(mbuf_t m);                         /* a received 802.3 frame */
size_t begonia_wlan_stats(char *out, size_t len);

/* BegoniaWLAN.cpp */
void begonia_wlan_registry_link(bool up);

#ifdef __cplusplus
}
#endif

#endif /* _BEGONIA_WLAN_H_ */
