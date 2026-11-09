/*
 * Begonia (MT6785): WPA2-PSK supplicant in the kernel.
 * IEEE 802.11-2016 12.7 CCMP 4-way and group key handshakes.
 */
#ifndef _BEGONIA_WPA_H_
#define _BEGONIA_WPA_H_

#include <sys/types.h>
#include <stdint.h>

#define BEGONIA_WPA_SEND   1       /* w->out / w->out_len: EAPOL frame to send to the AP */
#define BEGONIA_WPA_PTK    2       /* install w->ptk + 32 (TK, 16 bytes), pairwise, key id 0 */
#define BEGONIA_WPA_GTK    4       /* install w->gtk (gtk_len) as group key gtk_idx, RX PN from gtk_rsc */

struct begonia_wpa {
	uint8_t  pmk[32];
	uint8_t  aa[6], spa[6];         /* AP (authenticator) and our address */
	uint8_t  anonce[32], snonce[32];
	uint8_t  ptk[48];               /* KCK | KEK | TK */
	uint8_t  replay[8];
	uint8_t  rsnie[64], rsnie_len;  /* our RSN IE exactly as in the association request */
	uint8_t  gtk[32], gtk_len, gtk_idx, gtk_rsc[8];
	uint8_t  have_ptk, replay_ok, state;     /* state 0 idle, 1 sent message 2, 2 keys installed */
	uint32_t drops, mic_fail;
	uint8_t  out[256];
	uint32_t out_len;
};

void begonia_wpa_pmk(const char *pass, const uint8_t *ssid, size_t ssid_len, uint8_t pmk[32]);
void begonia_wpa_start(struct begonia_wpa *w, const uint8_t pmk[32], const uint8_t aa[6], const uint8_t spa[6],
    const uint8_t *rsnie, size_t rsnie_len);
int  begonia_wpa_rx(struct begonia_wpa *w, const uint8_t *frame, size_t len);

#endif /* _BEGONIA_WPA_H_ */
