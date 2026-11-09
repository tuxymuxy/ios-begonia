/*
 * Begonia (MT6785): CONNSYS / Wi-Fi hardware station driver header.
 */
#ifndef _BEGONIA_WIFI_H_
#define _BEGONIA_WIFI_H_

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/kpi_mbuf.h>
#include "begonia_wlan.h"

int  begonia_wifi_set_power(bool on);
int  begonia_wifi_scan(const struct vwl_scan *s);
int  begonia_wifi_scan_result(uint32_t i, struct vwl_bss *out);
int  begonia_wifi_current_bss(struct vwl_bss *out);
int  begonia_wifi_join(const struct vwl_join *j);
void begonia_wifi_leave(void);
void begonia_wifi_get_status(struct vwl_status *s);
void begonia_wifi_output(mbuf_t m);
void begonia_wifi_mac(uint8_t mac[6]);
void begonia_wifi_link(const char *args, char *out, size_t len);

#endif /* _BEGONIA_WIFI_H_ */
