/*
 * begonia: WPA2-PSK (CCMP) supplicant in the kernel, for the WiFi driver (bsd/dev/viva_wifi.c).
 *
 * iOS hands the PMK to the driver in APPLE80211_IOC_ASSOCIATE (wifid computes it), so the 4-way and group handshakes
 * run here, as on Apple's own chips (their firmware or driver does them). IEEE 802.11-2016 12.7:
 *   PMK      = PBKDF2-HMAC-SHA1(passphrase, SSID, 4096, 32)          (only for the USB link's "wifi join")
 *   PTK      = PRF-384(PMK, "Pairwise key expansion", min(AA,SPA) | max(AA,SPA) | min(ANonce,SNonce) | max(...))
 *              = KCK (16) | KEK (16) | TK (16)
 *   MIC      = HMAC-SHA1(KCK, EAPOL frame with MIC zeroed)[0..15]     (key descriptor version 2)
 *   key data = AES key wrap (RFC 3394) with KEK, holding the GTK KDE (dd len 00-0f-ac 01 keyid 00 GTK)
 * EAPOL-Key frame: 802.1X header (version, type 3, length BE) then descriptor type 2, key info BE, key length BE,
 * replay counter (8), nonce (32), key IV (16), RSC (8), reserved (8), MIC (16), key data length BE, key data.
 *
 * begonia_wpa_rx() takes one EAPOL frame from the AP (starting at the 802.1X header) and says what to do: send the
 * reply it built, install the PTK, install the GTK. Messages are checked (MIC, replay counter, ANonce kept between
 * 1 and 3) and anything wrong is dropped, as the standard says.
 */
#if 1
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/random.h>
#include <libkern/crypto/sha1.h>
#include <libkern/crypto/aes.h>

#include "begonia_wpa.h"

#define KI_VER_MASK     0x0007
#define KI_PAIRWISE     0x0008
#define KI_INSTALL      0x0040
#define KI_ACK          0x0080
#define KI_MIC          0x0100
#define KI_SECURE       0x0200
#define KI_ERROR        0x0400
#define KI_REQUEST      0x0800
#define KI_ENCDATA      0x1000

/* offsets in the EAPOL frame (802.1X header included) */
#define EK_DESC         4
#define EK_INFO         5
#define EK_KEYLEN       7
#define EK_REPLAY       9
#define EK_NONCE        17
#define EK_IV           49
#define EK_RSC          65
#define EK_MIC          81
#define EK_DATALEN      97
#define EK_DATA         99

static void
hmac_sha1(const uint8_t *key, size_t klen, const uint8_t *const *parts, const size_t *lens, int n, uint8_t out[20])
{
	uint8_t k[64], pad[64], inner[20];
	SHA1_CTX c;

	bzero(k, sizeof(k));
	if (klen > 64) {
		SHA1Init(&c);
		SHA1Update(&c, key, klen);
		SHA1Final(k, &c);
	} else {
		memcpy(k, key, klen);
	}
	for (int i = 0; i < 64; i++) {
		pad[i] = k[i] ^ 0x36;
	}
	SHA1Init(&c);
	SHA1Update(&c, pad, 64);
	for (int i = 0; i < n; i++) {
		SHA1Update(&c, parts[i], lens[i]);
	}
	SHA1Final(inner, &c);
	for (int i = 0; i < 64; i++) {
		pad[i] = k[i] ^ 0x5c;
	}
	SHA1Init(&c);
	SHA1Update(&c, pad, 64);
	SHA1Update(&c, inner, 20);
	SHA1Final(out, &c);
	bzero(k, sizeof(k));
	bzero(pad, sizeof(pad));
}

void
begonia_wpa_pmk(const char *pass, const uint8_t *ssid, size_t ssid_len, uint8_t pmk[32])
{
	size_t plen = strlen(pass);
	for (uint32_t block = 1; block <= 2; block++) {
		uint8_t cnt[4] = { 0, 0, 0, (uint8_t)block }, u[20], t[20];
		const uint8_t *p1[2] = { ssid, cnt };
		size_t l1[2] = { ssid_len, 4 };
		hmac_sha1((const uint8_t *)pass, plen, p1, l1, 2, u);
		memcpy(t, u, 20);
		for (int i = 1; i < 4096; i++) {
			const uint8_t *p[1] = { u };
			size_t l[1] = { 20 };
			hmac_sha1((const uint8_t *)pass, plen, p, l, 1, u);
			for (int j = 0; j < 20; j++) {
				t[j] ^= u[j];
			}
		}
		memcpy(pmk + (block - 1) * 20, t, block == 1 ? 20 : 12);
	}
}

static void
prf_sha1(const uint8_t *key, size_t klen, const char *label, const uint8_t *data, size_t dlen, uint8_t *out, size_t olen)
{
	uint8_t zero = 0, h[20];
	for (uint8_t i = 0; olen > 0; i++) {
		const uint8_t *p[4] = { (const uint8_t *)label, &zero, data, &i };
		size_t l[4] = { strlen(label), 1, dlen, 1 };
		hmac_sha1(key, klen, p, l, 4, h);
		size_t n = MIN(olen, (size_t)20);
		memcpy(out, h, n);
		out += n;
		olen -= n;
	}
}

/* RFC 3394 unwrap of n 64-bit blocks (in has n + 1); 0 when the integrity check passes. */
static int
aes_unwrap(const uint8_t kek[16], const uint8_t *in, size_t n, uint8_t *out)
{
	aes_decrypt_ctx ctx[1];
	uint8_t a[8], b[16], iv[16];

	if (n < 2 || n > 64) {
		return -1;
	}
	bzero(iv, sizeof(iv));
	aes_decrypt_key128(kek, ctx);
	memcpy(a, in, 8);
	memcpy(out, in + 8, n * 8);
	for (int j = 5; j >= 0; j--) {
		for (size_t i = n; i >= 1; i--) {
			uint64_t t = (uint64_t)n * (uint64_t)j + i;
			for (int k = 0; k < 8; k++) {
				a[7 - k] ^= (uint8_t)(t >> (8 * k));
			}
			memcpy(b, a, 8);
			memcpy(b + 8, out + (i - 1) * 8, 8);
			aes_decrypt_cbc(b, iv, 1, b, ctx);      /* one block, zero IV = ECB */
			memcpy(a, b, 8);
			memcpy(out + (i - 1) * 8, b + 8, 8);
		}
	}
	bzero(ctx, sizeof(ctx));
	for (int k = 0; k < 8; k++) {
		if (a[k] != 0xa6) {
			return -1;
		}
	}
	return 0;
}

static uint16_t
be16(const uint8_t *p)
{
	return (uint16_t)(p[0] << 8 | p[1]);
}

static void
mic(const uint8_t kck[16], uint8_t *frame, size_t len, uint8_t out[16])
{
	uint8_t saved[16], h[20];
	memcpy(saved, frame + EK_MIC, 16);
	bzero(frame + EK_MIC, 16);
	const uint8_t *p[1] = { frame };
	size_t l[1] = { len };
	hmac_sha1(kck, 16, p, l, 1, h);
	memcpy(frame + EK_MIC, saved, 16);
	memcpy(out, h, 16);
}

static int
cmp6(const uint8_t *a, const uint8_t *b, size_t n)
{
	return memcmp(a, b, n);
}

static void
derive_ptk(struct begonia_wpa *w)
{
	uint8_t data[6 + 6 + 32 + 32];
	const uint8_t *lo = cmp6(w->aa, w->spa, 6) < 0 ? w->aa : w->spa, *hi = lo == w->aa ? w->spa : w->aa;
	memcpy(data, lo, 6);
	memcpy(data + 6, hi, 6);
	const uint8_t *nlo = memcmp(w->anonce, w->snonce, 32) < 0 ? w->anonce : w->snonce;
	const uint8_t *nhi = nlo == w->anonce ? w->snonce : w->anonce;
	memcpy(data + 12, nlo, 32);
	memcpy(data + 44, nhi, 32);
	prf_sha1(w->pmk, 32, "Pairwise key expansion", data, sizeof(data), w->ptk, 48);
	bzero(data, sizeof(data));
}

/* Build an EAPOL-Key reply in w->out: key info, replay counter, nonce (or zeros), key data. MIC with the KCK. */
static void
build(struct begonia_wpa *w, uint16_t info, const uint8_t *replay, const uint8_t *nonce, const uint8_t *kd, size_t kdlen)
{
	uint8_t *f = w->out;
	size_t body = EK_DATA - 4 + kdlen;
	bzero(f, EK_DATA + kdlen);
	f[0] = 1;                       /* 802.1X-2001: what most APs and wpa_supplicant send */
	f[1] = 3;
	f[2] = (uint8_t)(body >> 8);
	f[3] = (uint8_t)body;
	f[EK_DESC] = 2;
	f[EK_INFO] = (uint8_t)(info >> 8);
	f[EK_INFO + 1] = (uint8_t)info;
	memcpy(f + EK_REPLAY, replay, 8);
	if (nonce) {
		memcpy(f + EK_NONCE, nonce, 32);
	}
	f[EK_DATALEN] = (uint8_t)(kdlen >> 8);
	f[EK_DATALEN + 1] = (uint8_t)kdlen;
	if (kdlen) {
		memcpy(f + EK_DATA, kd, kdlen);
	}
	w->out_len = EK_DATA + kdlen;
	mic(w->ptk, f, w->out_len, f + EK_MIC);
}

/* Find the GTK KDE in decrypted key data. */
static int
find_gtk(struct begonia_wpa *w, const uint8_t *kd, size_t len)
{
	for (size_t i = 0; i + 2 <= len;) {
		uint8_t id = kd[i], l = kd[i + 1];
		if (id == 0xdd && l == 0 && i + 2 == len) {
			break;                  /* padding */
		}
		if (i + 2 + l > len) {
			break;
		}
		if (id == 0xdd && l >= 6 + 16 && kd[i + 2] == 0x00 && kd[i + 3] == 0x0f && kd[i + 4] == 0xac &&
		    kd[i + 5] == 1) {
			size_t glen = l - 6;
			if (glen > sizeof(w->gtk)) {
				return -1;
			}
			w->gtk_idx = kd[i + 6] & 3;
			w->gtk_len = (uint8_t)glen;
			memcpy(w->gtk, kd + i + 8, glen);
			return 0;
		}
		i += 2 + l;
	}
	return -1;
}

void
begonia_wpa_start(struct begonia_wpa *w, const uint8_t pmk[32], const uint8_t aa[6], const uint8_t spa[6],
    const uint8_t *rsnie, size_t rsnie_len)
{
	bzero(w, sizeof(*w));
	memcpy(w->pmk, pmk, 32);
	memcpy(w->aa, aa, 6);
	memcpy(w->spa, spa, 6);
	if (rsnie_len > sizeof(w->rsnie)) {
		rsnie_len = sizeof(w->rsnie);
	}
	memcpy(w->rsnie, rsnie, rsnie_len);
	w->rsnie_len = (uint8_t)rsnie_len;
}

int
begonia_wpa_rx(struct begonia_wpa *w, const uint8_t *frame, size_t len)
{
	uint8_t f[512], m[16], kd[256];
	int act = 0;

	if (len < EK_DATA || len > sizeof(f) || frame[1] != 3 || (frame[EK_DESC] != 2 && frame[EK_DESC] != 254)) {
		w->drops++;
		return 0;
	}
	memcpy(f, frame, len);
	uint16_t info = be16(f + EK_INFO), kdlen = be16(f + EK_DATALEN);
	if ((info & KI_VER_MASK) != 2 || !(info & KI_ACK) || EK_DATA + (size_t)kdlen > len) {
		w->drops++;
		return 0;
	}
	/* replay counter strictly increasing once one was accepted with a MIC */
	if (w->replay_ok && memcmp(f + EK_REPLAY, w->replay, 8) <= 0) {
		w->drops++;
		return 0;
	}
	if (info & KI_MIC) {
		if (!w->have_ptk) {
			w->drops++;
			return 0;
		}
		mic(w->ptk, f, len, m);
		if (memcmp(m, f + EK_MIC, 16) != 0) {
			w->mic_fail++;
			return 0;
		}
	}
	size_t kdl = kdlen;
	const uint8_t *kdp = f + EK_DATA;
	if ((info & KI_ENCDATA) && kdlen) {
		if (!(info & KI_MIC) || kdlen % 8 || kdlen < 24 || kdlen - 8 > sizeof(kd) ||
		    aes_unwrap(w->ptk + 16, f + EK_DATA, kdlen / 8 - 1, kd) != 0) {
			w->drops++;
			return 0;
		}
		kdp = kd;
		kdl = kdlen - 8;
	}

	if ((info & KI_PAIRWISE) && !(info & KI_MIC)) {
		/* message 1: new SNonce, PTK, message 2 with our RSN IE */
		memcpy(w->anonce, f + EK_NONCE, 32);
		read_random(w->snonce, 32);
		derive_ptk(w);
		w->have_ptk = 1;
		build(w, 2 | KI_PAIRWISE | KI_MIC, f + EK_REPLAY, w->snonce, w->rsnie, w->rsnie_len);
		w->state = 1;
		act |= BEGONIA_WPA_SEND;
	} else if ((info & KI_PAIRWISE) && (info & KI_INSTALL)) {
		/* message 3: same ANonce as message 1, GTK in the encrypted key data, message 4, install both */
		if (w->state < 1 || memcmp(w->anonce, f + EK_NONCE, 32) != 0) {
			w->drops++;
			return 0;
		}
		memcpy(w->replay, f + EK_REPLAY, 8);
		w->replay_ok = 1;
		if ((info & KI_ENCDATA) && find_gtk(w, kdp, kdl) == 0) {
			memcpy(w->gtk_rsc, f + EK_RSC, 8);
			act |= BEGONIA_WPA_GTK;
		}
		build(w, 2 | KI_PAIRWISE | KI_MIC | KI_SECURE, f + EK_REPLAY, NULL, NULL, 0);
		w->state = 2;
		act |= BEGONIA_WPA_SEND | BEGONIA_WPA_PTK;
	} else if (!(info & KI_PAIRWISE) && (info & KI_MIC) && w->state == 2) {
		/* group key handshake message 1 (rekey) */
		memcpy(w->replay, f + EK_REPLAY, 8);
		w->replay_ok = 1;
		if (find_gtk(w, kdp, kdl) != 0) {
			w->drops++;
			return 0;
		}
		memcpy(w->gtk_rsc, f + EK_RSC, 8);
		build(w, 2 | KI_MIC | KI_SECURE, f + EK_REPLAY, NULL, NULL, 0);
		act |= BEGONIA_WPA_SEND | BEGONIA_WPA_GTK;
	} else {
		w->drops++;
	}
	bzero(f, sizeof(f));
	bzero(kd, sizeof(kd));
	return act;
}
#endif /* 1 */
