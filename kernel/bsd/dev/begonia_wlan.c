/*
 * viva (MT6781): the WiFi station as iOS 16 expects it (no IO80211Family, no Skywalk).
 *
 * IO80211.framework (wifid) falls back to the classic ioctl path when IOServiceOpen fails on the interface's
 * IOService (iokit/Kernel/arm/VivaWLAN.cpp has no user client): SIOCGA80211 / SIOCSA80211 on an AF_INET socket
 * with struct apple80211req (40 bytes, req_data is a user pointer), and events on the kern_event socket
 * (vendor 1, class 6 KEV_IEEE80211_CLASS, subclass 1, data = interface name[16] + payload).
 * Layouts and the call sequence: ~/Developer/ios-viva/wifi/docs/IOS16-WIFI-ABI.md + apple80211_ios16.h
 * (recovered from 20H392's IO80211.framework and wifid).
 *
 * en0: IFT_ETHER, IFNET_SUBFAMILY_WIFI, SIOCGIFMEDIA = IFM_IEEE80211 (Apple80211BindToInterfaceWithIOCTL keeps
 * only interfaces with that media type), BPF (IPConfiguration's DHCP), KEV_DL_LINK_ON/OFF (configd).
 * The station (scan, join, WPA2, data) is bsd/dev/viva_wifi.c; every request here is either a snapshot read
 * or queued to its worker, so wifid never waits on the hardware.
 *
 * Which requests are answered, the event payloads and their order follow hakios-xnu-kernel and ios-sapphire (the
 * same 20H392 userland joining from Settings): ~/Developer/ios-viva/wifi/docs/IOS16-WIFI-HAKIOS.md. In short:
 * SCAN_DONE / ASSOC_DONE / POWER_CHANGED have no payload; a join ends KEV_DL_LINK_ON, ASSOC_DONE, SSID_CHANGED,
 * BSSID_CHANGED, LINK_CHANGED(up); RSSI_BOUNDS and NOISE must be answered or auto-join sees every network "below
 * minimum RSSI"; joined-only GETs say ENXIO when not joined; everything unknown is EOPNOTSUPP.
 */
#if 1
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/sockio.h>
#include <sys/kern_event.h>
#include <sys/kpi_mbuf.h>
#include <kern/locks.h>
#include <net/if.h>
#include <net/if_types.h>
#include <net/ethernet.h>
#include <net/if_ether.h>
#include <net/ether_if_module.h>
#include <net/kpi_interface.h>
#include <net/kpi_protocol.h>
#include <net/if_media.h>
#include <net/bpf.h>
#include <net/dlil.h>

#include "begonia_wpa.h"
#include "begonia_wlan.h"
#include "begonia_wifi.h"

struct apple80211req {
	char     req_if_name[IFNAMSIZ];
	int32_t  req_type;
	int32_t  req_val;
	uint32_t req_len;
	uint32_t req_pad;
	uint64_t req_data;
};
_Static_assert(sizeof(struct apple80211req) == 40, "apple80211req");

#define SIOCSA80211             _IOW('i', 200, struct apple80211req)
#define SIOCGA80211             _IOWR('i', 201, struct apple80211req)

#define IOC_SSID                1
#define IOC_AUTH_TYPE           2
#define IOC_CHANNEL             4
#define IOC_POWERSAVE           5
#define IOC_PROTMODE            6
#define IOC_TXPOWER             7
#define IOC_RATE                8
#define IOC_BSSID               9
#define IOC_SCAN_REQ            10
#define IOC_SCAN_RESULT         11
#define IOC_CARD_CAPABILITIES   12
#define IOC_STATE               13
#define IOC_PHY_MODE            14
#define IOC_OP_MODE             15
#define IOC_RSSI                16
#define IOC_NOISE               17
#define IOC_INT_MIT             18
#define IOC_POWER               19
#define IOC_ASSOCIATE           20
#define IOC_ASSOCIATE_RESULT    21
#define IOC_DISASSOCIATE        22
#define IOC_SUPPORTED_CHANNELS  27
#define IOC_LOCALE              28
#define IOC_DEAUTH              29
#define IOC_DRIVER_VERSION      43
#define IOC_HARDWARE_VERSION    44
#define IOC_BACKGROUND_SCAN     47
#define IOC_ASSOCIATION_STATUS  50
#define IOC_COUNTRY_CODE        51
#define IOC_MCS                 57
#define IOC_CURRENT_NETWORK     103
#define IOC_RSSI_BOUNDS         106
#define IOC_CHANNELS_INFO       207
#define IOC_HW_SUPPORTED_CHANNELS 254
#define IOC_ABORT_SCAN          382

#define M_POWER_CHANGED         1
#define M_SSID_CHANGED          2
#define M_BSSID_CHANGED         3
#define M_LINK_CHANGED          4
#define M_ASSOC_DONE            9
#define M_SCAN_DONE             10

#define CH_20MHZ                0x2
#define CH_2GHZ                 0x8
#define CH_5GHZ                 0x10
#define CH_ACTIVE               0x80
#define CH_DFS                  0x100

#define SCAN_DATA_SIZE          5456
#define SCAN_RESULT_SIZE        1164
#define ASSOC_DATA_SIZE         592
#define SUP_CHANNELS_SIZE       4824
#define CHANNELS_INFO_SIZE      10016

static struct {
	ifnet_t     ifp;
	lck_mtx_t  *mtx;
	uint32_t    scanIdx;            /* next SCAN_RESULT */
	bool        link;
	uint8_t     logged[64];         /* unknown request types already logged (bit per type < 512) */
	uint32_t    gets, sets, unknown;
} wn;

static void
wn_event(uint32_t code, const void *payload, uint32_t plen)
{
	struct kev_msg ev;
	char name[IFNAMSIZ];

	bzero(&ev, sizeof(ev));
	bzero(name, sizeof(name));
	strlcpy(name, "en0", sizeof(name));
	ev.vendor_code = KEV_VENDOR_APPLE;
	ev.kev_class = KEV_IEEE80211_CLASS;
	ev.kev_subclass = 1;
	ev.event_code = code;
	ev.dv[0].data_length = IFNAMSIZ;
	ev.dv[0].data_ptr = name;
	if (plen) {
		ev.dv[1].data_length = plen;
		ev.dv[1].data_ptr = (void *)(uintptr_t)payload;
	}
	(void)kev_post_msg(&ev);
}

static void
wn_log_once(bool set, int32_t type, uint32_t len)
{
	wn.unknown++;
	if (type >= 0 && type < 512) {
		uint8_t bit = (uint8_t)(1u << (type & 7));
		if (wn.logged[type >> 3] & bit) {
			return;
		}
		wn.logged[type >> 3] |= bit;
	}
	printf("BEGONIA WLAN: %s type %d len %u not supported\n", set ? "SET" : "GET", type, len);
}

/* ---- data ---- */

static errno_t
wn_output(ifnet_t ifp, mbuf_t m)
{
	bpf_tap_out(ifp, DLT_EN10MB, m, NULL, 0);
	if (!wn.link) {
		mbuf_freem(m);
		ifnet_stat_increment_out(ifp, 0, 0, 1);
		return 0;
	}
	ifnet_stat_increment_out(ifp, 1, mbuf_pkthdr_len(m), 0);
	begonia_wifi_output(m);
	return 0;
}

void
begonia_wlan_input(mbuf_t m)
{
	ifnet_t ifp = wn.ifp;
	if (ifp == NULL || mbuf_pkthdr_len(m) < ETHER_HDR_LEN || mbuf_len(m) < ETHER_HDR_LEN) {
		mbuf_freem(m);
		return;
	}
	struct ifnet_stat_increment_param stats = {};
	stats.packets_in = 1;
	stats.bytes_in = mbuf_pkthdr_len(m);
	mbuf_pkthdr_setrcvif(m, ifp);
	bpf_tap_in(ifp, DLT_EN10MB, m, NULL, 0);
	void *frame_header = mbuf_data(m);
	mbuf_pkthdr_setheader(m, frame_header);
	mbuf_pkthdr_adjustlen(m, -ETHER_HDR_LEN);
	(void)mbuf_setdata(m, (char *)mbuf_data(m) + ETHER_HDR_LEN, mbuf_len(m) - ETHER_HDR_LEN);
	ifnet_input(ifp, m, &stats);
}

/* ---- events from the station ---- */

void
begonia_wlan_power(bool on)
{
	(void)on;
	wn_event(M_POWER_CHANGED, NULL, 0);
}

void
begonia_wlan_scan_done(int status)
{
	lck_mtx_lock(wn.mtx);
	wn.scanIdx = 0;
	lck_mtx_unlock(wn.mtx);
	(void)status;
	wn_event(M_SCAN_DONE, NULL, 0);
}

static void
wn_link_event(bool up, uint32_t reason)
{
	struct vwl_status s;
	uint8_t d[32];
	begonia_wifi_get_status(&s);
	bzero(d, sizeof(d));
	d[0] = up ? 0 : 1;                                      /* isLinkDown */
	int32_t rssi = s.rssi;
	uint16_t snr = (uint16_t)MAX(0, MIN(90, s.rssi + 95)), nf = (uint16_t)-95;
	memcpy(d + 4, &rssi, 4);
	memcpy(d + 8, &snr, 2);
	memcpy(d + 0xa, &nf, 2);
	if (!up) {
		d[0x10] = reason != 0;                          /* involuntary */
		memcpy(d + 0x14, &reason, 4);
	}
	wn_event(M_LINK_CHANGED, d, sizeof(d));
}

void
begonia_wlan_assoc_done(int result)
{
	if (result == 1) {
		wn.link = true;
		if (wn.ifp != NULL) {
			dlil_post_msg(wn.ifp, KEV_DL_SUBCLASS, KEV_DL_LINK_ON, NULL, 0, FALSE);
		}
		begonia_wlan_registry_link(true);
		wn_event(M_ASSOC_DONE, NULL, 0);
		wn_event(M_SSID_CHANGED, NULL, 0);
		wn_event(M_BSSID_CHANGED, NULL, 0);
		wn_link_event(true, 0);
	} else {
		wn_event(M_ASSOC_DONE, NULL, 0);
	}
}

void
begonia_wlan_link(bool up, uint32_t reason)
{
	if (up || !wn.link) {
		return;
	}
	wn.link = false;
	if (wn.ifp != NULL) {
		dlil_post_msg(wn.ifp, KEV_DL_SUBCLASS, KEV_DL_LINK_OFF, NULL, 0, FALSE);
	}
	begonia_wlan_registry_link(false);
	wn_link_event(false, reason);
	wn_event(M_SSID_CHANGED, NULL, 0);
	wn_event(M_BSSID_CHANGED, NULL, 0);
}

/* ---- Apple80211 GET ---- */

static const uint8_t wn_ch5[] = { 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136,
	                          140, 144, 149, 153, 157, 161, 165 };

static uint32_t
wn_nch(void)
{
	return 13 + sizeof(wn_ch5);
}

static uint32_t
wn_ch(uint32_t i)
{
	return i < 13 ? i + 1 : wn_ch5[i - 13];
}

static uint32_t
wn_ch_flags(uint32_t ch)
{
	if (ch <= 14) {
		return CH_2GHZ | CH_20MHZ | CH_ACTIVE;
	}
	return CH_5GHZ | CH_20MHZ | ((ch >= 52 && ch <= 144) ? CH_DFS : CH_ACTIVE);
}

/* struct apple80211_scan_result (1164 bytes) from a BSS */
static void
wn_scan_result(uint8_t *r, const struct vwl_bss *b, uint16_t room)
{
	static const uint8_t rate_ie[2] = { 1, 50 };
	bzero(r, SCAN_RESULT_SIZE);
	uint32_t v = 1, ch = b->ch, fl = wn_ch_flags(b->ch);
	memcpy(r + 0x00, &v, 4);
	memcpy(r + 0x04, &v, 4);
	memcpy(r + 0x08, &ch, 4);
	memcpy(r + 0x0c, &fl, 4);
	int16_t noise = -95, rssi = b->rssi, snr = (int16_t)MAX(0, MIN(90, b->rssi + 95));
	uint16_t one = 1;
	memcpy(r + 0x10, &one, 2);
	memcpy(r + 0x12, &noise, 2);
	memcpy(r + 0x14, &snr, 2);
	memcpy(r + 0x16, &rssi, 2);
	memcpy(r + 0x18, &b->bintval, 2);
	memcpy(r + 0x1a, &b->capinfo, 2);
	memcpy(r + 0x1c, b->bssid, 6);
	/* rates (Mbit/s) from the supported / extended rates IEs */
	uint32_t n = 0;
	for (int k = 0; k < 2; k++) {
		for (uint32_t i = 0; i + 2 <= b->ie_len && i + 2 + b->ie[i + 1] <= b->ie_len; i += 2 + b->ie[i + 1]) {
			if (b->ie[i] != rate_ie[k]) {
				continue;
			}
			for (uint32_t j = 0; j < b->ie[i + 1] && n < 15; j++) {
				uint32_t mbps = (b->ie[i + 2 + j] & 0x7f) / 2;
				if (mbps) {
					memcpy(r + 0x24 + 4 * n++, &mbps, 4);
				}
			}
		}
	}
	r[0x22] = (uint8_t)n;
	r[0x60] = b->ssid_len;
	memcpy(r + 0x61, b->ssid, b->ssid_len);
	memcpy(r + 0x84, &b->age_ms, 4);
	uint16_t ielen = b->ie_len <= room ? b->ie_len : 0;     /* all of them or none (hakios) */
	memcpy(r + 0x8a, &ielen, 2);
	memcpy(r + 0x8c, b->ie, ielen);
}

static int
wn_out(struct apple80211req *req, const void *buf, uint32_t n)
{
	if (req->req_data == 0) {
		return EINVAL;
	}
	n = MIN(n, req->req_len);
	int err = copyout(buf, (user_addr_t)req->req_data, n);
	req->req_len = n;
	return err;
}

static int
wn_get(struct apple80211req *req)
{
	struct vwl_status s;
	begonia_wifi_get_status(&s);
	bool assoc = s.link;
	uint8_t b[64];
	uint32_t *w = (uint32_t *)(void *)b;
	bzero(b, sizeof(b));
	wn.gets++;

	switch (req->req_type) {
	case IOC_SSID:
		if (!assoc) {
			return ENXIO;
		}
		return wn_out(req, s.ssid, s.ssid_len);
	case IOC_BSSID:
		if (!assoc) {
			return ENXIO;
		}
		return wn_out(req, s.bssid, 6);
	case IOC_AUTH_TYPE:
		if (!assoc) {
			return ENXIO;
		}
		w[0] = 1;
		w[1] = 1;                                       /* open system */
		w[2] = s.rsn ? 0x8 : 0;                         /* WPA2-PSK */
		return wn_out(req, b, 16);
	case IOC_CHANNEL:
		if (!assoc) {
			return ENXIO;
		}
		w[0] = 1;
		w[1] = 1;
		w[2] = s.ch;
		w[3] = wn_ch_flags(s.ch);
		return wn_out(req, b, 16);
	case IOC_RSSI:
	case IOC_NOISE: {
		/* NOISE at any time (ios-sapphire 0294: auto-join needs it), RSSI only when joined */
		if (req->req_type == IOC_RSSI && !assoc) {
			return ENXIO;
		}
		int32_t v = req->req_type == IOC_RSSI ? s.rssi : -95;
		w[0] = 1;
		w[1] = 1;
		w[2] = 0;                                       /* dBm */
		memcpy(b + 12, &v, 4);                          /* rssi[0] */
		memcpy(b + 28, &v, 4);                          /* aggregate */
		memcpy(b + 32, &v, 4);                          /* rssi_ext[0] */
		memcpy(b + 48, &v, 4);                          /* aggregate_ext */
		return wn_out(req, b, 52);
	}
	case IOC_RSSI_BOUNDS: {
		/* read once when wifid opens the interface; unanswered = 0 dBm threshold, nothing auto-joins */
		int32_t rb[4] = { 1, -95, -20, -85 };
		return wn_out(req, rb, sizeof(rb));
	}
	case IOC_STATE:
		req->req_val = s.state;
		req->req_len = 0;
		return 0;
	case IOC_OP_MODE:
		req->req_val = 1;                               /* STA */
		req->req_len = 0;
		return 0;
	case IOC_ASSOCIATE_RESULT:
		req->req_val = s.result;
		req->req_len = 0;
		return 0;
	case IOC_POWER:
		w[0] = 1;
		w[1] = 1;
		w[2] = s.power;                                 /* what was asked: the radio follows on the worker */
		return wn_out(req, b, 24);
	case IOC_CARD_CAPABILITIES: {
		/* 13 raw bytes (req_data is past the version word): AES, AES_CCM, PMGT, SHSLOT, SHPREAMBLE, WPA2, WME,
		 * SHORT_GI_20 = 4C 46 0A 00 ... (hakios: other bits and the framework sees no WPA2, "Unable to join") */
		static const uint8_t bits[] = { 2, 3, 6, 9, 10, 14, 17, 19 };
		for (unsigned i = 0; i < sizeof(bits); i++) {
			b[bits[i] >> 3] |= (uint8_t)(1u << (bits[i] & 7));
		}
		return wn_out(req, b, 13);
	}
	case IOC_DRIVER_VERSION:
	case IOC_HARDWARE_VERSION: {
		char v[256];
		bzero(v, sizeof(v));
		strlcpy(v, "VivaWLAN: MT6781 CONNSYS (CONNAC1) + MT6631", sizeof(v));
		return wn_out(req, v, sizeof(v));
	}
	case IOC_SUPPORTED_CHANNELS:
	case IOC_HW_SUPPORTED_CHANNELS: {
		uint8_t *c = kalloc_data(SUP_CHANNELS_SIZE, Z_WAITOK | Z_ZERO);
		if (c == NULL) {
			return ENOMEM;
		}
		uint32_t n = 0, v = 1;
		memcpy(c, &v, 4);
		for (uint32_t i = 0; i < wn_nch(); i++, n++) {
			uint32_t ch = wn_ch(i), e[3] = { 1, ch, wn_ch_flags(ch) };
			memcpy(c + 8 + 12 * n, e, 12);
		}
		memcpy(c + 4, &n, 4);
		int err = wn_out(req, c, SUP_CHANNELS_SIZE);
		kfree_data(c, SUP_CHANNELS_SIZE);
		return err;
	}
	case IOC_CHANNELS_INFO: {
		/* hakios layout: u16 count @0xc, u8 number[400] @0x32e, indoor @0x4be, dfs @0x64e, passive @0x7de,
		 * wide40 @0x96e, wide80 @0xafe, u32 flags[400] @0x12d0; no version word */
		uint8_t *c = kalloc_data(CHANNELS_INFO_SIZE, Z_WAITOK | Z_ZERO);
		if (c == NULL) {
			return ENOMEM;
		}
		uint16_t n = (uint16_t)wn_nch();
		memcpy(c + 0xc, &n, 2);
		for (uint32_t i = 0; i < n; i++) {
			uint32_t ch = wn_ch(i), fl = wn_ch_flags(ch);
			c[0x32e + i] = (uint8_t)ch;
			c[0x64e + i] = (fl & CH_DFS) ? 1 : 0;
			c[0x7de + i] = (fl & CH_ACTIVE) ? 0 : 1;
			memcpy(c + 0x12d0 + 4 * i, &fl, 4);
		}
		int err = wn_out(req, c, CHANNELS_INFO_SIZE);
		kfree_data(c, CHANNELS_INFO_SIZE);
		return err;
	}
	case IOC_SCAN_RESULT:
	case IOC_CURRENT_NETWORK: {
		if (req->req_type == IOC_CURRENT_NETWORK && !assoc) {
			return ENXIO;
		}
		struct vwl_bss *bss = kalloc_type(struct vwl_bss, Z_WAITOK | Z_ZERO);
		uint8_t *r = kalloc_data(SCAN_RESULT_SIZE, Z_WAITOK);
		int err = ENOMEM;
		if (bss != NULL && r != NULL) {
			if (req->req_type == IOC_CURRENT_NETWORK) {
				err = begonia_wifi_current_bss(bss);
			} else {
				lck_mtx_lock(wn.mtx);
				uint32_t i = wn.scanIdx++;
				lck_mtx_unlock(wn.mtx);
				err = begonia_wifi_scan_result(i, bss) == 0 ? 0 : EIO;
			}
			if (err == 0) {
				/* the caller's ie_len (+0x8a) is the room it has for the IEs */
				uint16_t room = 1024;
				if (req->req_data != 0 && req->req_len >= 0x8c) {
					(void)copyin((user_addr_t)req->req_data + 0x8a, &room, 2);
				}
				wn_scan_result(r, bss, MIN(room, (uint16_t)1024));
				err = wn_out(req, r, SCAN_RESULT_SIZE);
			}
		}
		if (r != NULL) {
			kfree_data(r, SCAN_RESULT_SIZE);
		}
		if (bss != NULL) {
			kfree_type(struct vwl_bss, bss);
		}
		return err;
	}
	default:
		wn_log_once(false, req->req_type, req->req_len);
		return EOPNOTSUPP;
	}
}

/* ---- Apple80211 SET ---- */

static int
wn_in(const struct apple80211req *req, void *buf, uint32_t n)
{
	if (req->req_data == 0 || req->req_len < n) {
		return EINVAL;
	}
	return copyin((user_addr_t)req->req_data, buf, n);
}

static int
wn_set_scan(const struct apple80211req *req)
{
	if (req->req_len != SCAN_DATA_SIZE) {
		return EINVAL;
	}
	uint8_t *d = kalloc_data(SCAN_DATA_SIZE, Z_WAITOK | Z_ZERO);
	if (d == NULL) {
		return ENOMEM;
	}
	int err = wn_in(req, d, SCAN_DATA_SIZE);
	if (err == 0) {
		struct vwl_scan s;
		bzero(&s, sizeof(s));
		uint32_t sl, type, nch;
		memcpy(&sl, d + 0x10, 4);
		memcpy(&type, d + 0x34, 4);
		memcpy(&nch, d + 0x44, 4);
		if (sl > 0 && sl <= 32) {
			s.ssid_len = (uint8_t)sl;
			memcpy(s.ssid, d + 0x14, sl);
		}
		s.passive = type == 2;
		if (nch <= sizeof(s.ch)) {                     /* longer lists: every channel */
			for (uint32_t i = 0; i < nch; i++) {
				uint32_t ch;
				memcpy(&ch, d + 0x48 + 12 * i + 4, 4);
				if (ch > 0 && ch <= 196) {
					s.ch[s.nch++] = (uint8_t)ch;
				}
			}
		}
		lck_mtx_lock(wn.mtx);
		wn.scanIdx = 0;
		lck_mtx_unlock(wn.mtx);
		err = begonia_wifi_scan(&s);
	}
	kfree_data(d, SCAN_DATA_SIZE);
	return err;
}

static int
wn_set_assoc(const struct apple80211req *req)
{
	uint8_t *d = kalloc_data(ASSOC_DATA_SIZE, Z_WAITOK | Z_ZERO);
	if (d == NULL) {
		return ENOMEM;
	}
	int err = req->req_len >= 0x88 ? wn_in(req, d, MIN(req->req_len, (uint32_t)ASSOC_DATA_SIZE)) : EINVAL;
	if (err == 0) {
		struct vwl_join j;
		bzero(&j, sizeof(j));
		uint32_t sl, klen, cipher;
		memcpy(&sl, d + 0xc, 4);
		memcpy(&klen, d + 0x3c, 4);
		memcpy(&cipher, d + 0x40, 4);
		if (sl == 0 || sl > 32) {
			err = EINVAL;
			goto done;
		}
		j.ssid_len = (uint8_t)sl;
		memcpy(j.ssid, d + 0x10, sl);
		memcpy(j.bssid, d + 0x30, 6);
		if (klen == 0) {
			j.has_pmk = 0;                          /* open */
		} else if (cipher == 10 && klen >= 8 && klen <= 63) {
			char pass[64];
			memcpy(pass, d + 0x48, klen);
			pass[klen] = 0;
			begonia_wpa_pmk(pass, j.ssid, j.ssid_len, j.pmk);
			bzero(pass, sizeof(pass));
			j.has_pmk = 1;
		} else if (cipher == 6 && klen == 32) {
			memcpy(j.pmk, d + 0x48, 32);
			j.has_pmk = 1;
		} else {
			err = EINVAL;
			goto done;
		}
		printf("BEGONIA WLAN: ASSOCIATE \"%.*s\" %02x:%02x:%02x:%02x:%02x:%02x %s\n", j.ssid_len, j.ssid, j.bssid[0],
		    j.bssid[1], j.bssid[2], j.bssid[3], j.bssid[4], j.bssid[5], !j.has_pmk ? "open" :
		    cipher == 10 ? "passphrase" : "PMK");
		err = begonia_wifi_join(&j);
done:
		bzero(&j, sizeof(j));
	}
	bzero(d, ASSOC_DATA_SIZE);
	kfree_data(d, ASSOC_DATA_SIZE);
	return err;
}

static int
wn_set(struct apple80211req *req)
{
	wn.sets++;
	switch (req->req_type) {
	case IOC_POWER: {
		uint32_t p[6];
		int err = wn_in(req, p, sizeof(p));
		if (err != 0) {
			return err;
		}
		bool on = p[2] != 0;
		printf("BEGONIA WLAN: SET POWER %s\n", on ? "on" : "off");
		return begonia_wifi_set_power(on);
	}
	case IOC_SCAN_REQ:
		return wn_set_scan(req);
	case IOC_ASSOCIATE:
		return wn_set_assoc(req);
	case IOC_DISASSOCIATE:
		printf("BEGONIA WLAN: DISASSOCIATE\n");
		begonia_wifi_leave();
		return 0;
	default:
		wn_log_once(true, req->req_type, req->req_len);
		return EOPNOTSUPP;
	}
}

/* ---- ifnet ---- */

static errno_t
wn_ioctl(ifnet_t ifp, unsigned long cmd, void *data)
{
	switch (cmd) {
	case SIOCGA80211:
		return wn_get((struct apple80211req *)data);
	case SIOCSA80211:
		return wn_set((struct apple80211req *)data);
	case SIOCGIFMEDIA32:
	case SIOCGIFMEDIA64: {
		struct ifmediareq *ifmr = (struct ifmediareq *)data;
		user_addr_t list = (cmd == SIOCGIFMEDIA64) ? ((struct ifmediareq64 *)ifmr)->ifmu_ulist :
		    CAST_USER_ADDR_T(((struct ifmediareq32 *)ifmr)->ifmu_ulist);
		int media = IFM_IEEE80211 | IFM_AUTO;
		ifmr->ifm_current = media;
		ifmr->ifm_active = media;
		ifmr->ifm_mask = 0;
		ifmr->ifm_status = IFM_AVALID | (wn.link ? IFM_ACTIVE : 0);
		if (list != USER_ADDR_NULL && ifmr->ifm_count > 0) {
			ifmr->ifm_count = 1;
			return copyout(&media, list, sizeof(media));
		}
		ifmr->ifm_count = 1;
		return 0;
	}
	case SIOCSIFADDR:
	case SIOCAIFADDR:
		ifnet_set_flags(ifp, IFF_UP, IFF_UP);
		return 0;
	case SIOCSIFFLAGS:
		ifnet_set_flags(ifp, (ifnet_flags(ifp) & IFF_UP) ? IFF_RUNNING : 0, IFF_RUNNING);
		return 0;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		return 0;
	default:
		return EOPNOTSUPP;
	}
}

static void
wn_detach(ifnet_t ifp __unused)
{
}

void
begonia_wlan_attach(void)
{
	struct ifnet_init_eparams init;
	static lck_grp_t *grp;
	uint8_t mac[6];

	if (wn.ifp != NULL) {
		return;
	}
	grp = lck_grp_alloc_init("viva_wlan", LCK_GRP_ATTR_NULL);
	wn.mtx = lck_mtx_alloc_init(grp, LCK_ATTR_NULL);
	begonia_wifi_mac(mac);

	bzero(&init, sizeof(init));
	init.ver = IFNET_INIT_CURRENT_VERSION;
	init.len = sizeof(init);
	init.flags = IFNET_INIT_LEGACY | IFNET_INIT_NX_NOAUTO;      /* classic output(), no Skywalk nexus */
	init.uniqueid = "begonia-wlan";
	init.uniqueid_len = sizeof("begonia-wlan") - 1;
	init.name = "en";
	init.unit = 0;
	init.family = IFNET_FAMILY_ETHERNET;
	init.subfamily = IFNET_SUBFAMILY_WIFI;
	init.type = IFT_ETHER;
	init.output = wn_output;
	init.demux = ether_demux;
	init.add_proto = ether_add_proto;
	init.del_proto = ether_del_proto;
	init.check_multi = ether_check_multi;
	init.framer_extended = ether_frameout_extended;
	init.ioctl = wn_ioctl;
	init.detach = wn_detach;
	init.broadcast_addr = etherbroadcastaddr;
	init.broadcast_len = ETHER_ADDR_LEN;
	if (ifnet_allocate_extended(&init, &wn.ifp) != 0) {
		printf("BEGONIA WLAN: ifnet_allocate failed\n");
		wn.ifp = NULL;
		return;
	}
	ifnet_set_addrlen(wn.ifp, ETHER_ADDR_LEN);
	ifnet_set_hdrlen(wn.ifp, sizeof(struct ether_header));
	ifnet_set_mtu(wn.ifp, ETHERMTU);
	ifnet_set_baudrate(wn.ifp, 150000000);
	ifnet_set_flags(wn.ifp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST, 0xffff);
	if (ifnet_attach(wn.ifp, NULL) != 0) {
		printf("BEGONIA WLAN: ifnet_attach failed\n");
		ifnet_release(wn.ifp);
		wn.ifp = NULL;
		return;
	}
	ifnet_set_lladdr(wn.ifp, mac, ETHER_ADDR_LEN);
	bpfattach(wn.ifp, DLT_EN10MB, ETHER_HDR_LEN);
	printf("BEGONIA WLAN: en0 %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* USB link "wifi wlan": what wifid asked */
size_t
begonia_wlan_stats(char *out, size_t len)
{
	size_t o = 0;
	if (len) {
		o = (size_t)snprintf(out, len, "wlan: en0 %s, link %u, %u GETs, %u SETs, %u unsupported\n",
		    wn.ifp ? "attached" : "MISSING", wn.link, wn.gets, wn.sets, wn.unknown);
	}
	return MIN(o, len);
}

void
begonia_wlan_mac(uint8_t mac[6])
{
	begonia_wifi_mac(mac);
}

void
begonia_wlan_init(void)
{
    begonia_wlan_attach();
    begonia_wifi_set_power(true);
}

#endif
