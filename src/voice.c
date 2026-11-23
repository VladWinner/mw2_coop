// Push-to-talk voice between the two players of a network co-op session (owner 2026-11-23, the night list: "voice chat, a push-to-talk key, so that Discord is
// not needed").
//
// The game has no voice of its own to switch on: the single player exe ships the network's voice dvars and packet names with capture, codec and send compiled
// out (research 2026-11-23). So this is the mod's own, the same on the 64-bit, 32-bit and iw4x-sp builds (no game address):
// - capture: the real System32 winmm's waveIn (this DLL is winmm.dll: the functions are looked up in the system's), 16 kHz mono 16-bit, frames of 40 ms;
// - codec: IMA ADPCM (4:1), 320 bytes a frame, about 8.6 KB/s and only while the key is held (and 200 ms after);
// - transport: the mod's out-of-band packets on the game's socket to the partner ("mw2coop_v" + a small header + the frame): LAN, VPN, UPnP and the Steam relay
//   alike; taken off the socket before the network watch counts the partner's packets (voice comes from a thread of its own and would hide a frozen partner);
// - playback: waveOut, a jitter buffer that starts at 3 frames (120 ms) and keeps 10 at most, silence for a lost frame;
// - the key: [voice] talk_key (CAPSLOCK by default) bound once to +mw2coop_talk when the key is free; [voice] enabled, volume_pct (0..200), mic (a part of the
//   device's name; empty: Windows' default);
// - who talks: a line in the game's message window when the partner starts talking ("<name> talks"), this PC only (never through the scripts: the lockstep).
// Nothing in the split screen (both players at one PC). QA: mw2coop_voice_qa tone <hz> <ms> sends a generated tone instead of the microphone; the partner's
// log says every 5 s how many frames came, how many were lost and the share of a frequency in them (a Goertzel filter; mw2coop_voice_qa listen <hz>).
#include <windows.h>
#include <mmsystem.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "log.h"
#include "native.h"
#include "ctexts.h"
#include "localize.h"
#include "net.h"
#include "netwatch.h"
#include "rejoin.h"
#include "settings.h"
#include "voice.h"

#define RATE 16000
#define FRAME 640 // samples (40 ms)
#define CODED (FRAME / 2)
#define IN_BUFS 4
#define OUT_BUFS 6
#define JITTER_MAX 10
#define TAIL_MS 200
static const char TAG[] = "\xff\xff\xff\xffmw2coop_v";
#define TAG_LEN (sizeof(TAG) - 1)
#define HDR 12 // ver, flags, seq (2), ms (4), predictor (2), index, pad

// the real winmm (the system's)
static struct {
	MMRESULT(WINAPI *in_open)(LPHWAVEIN, UINT, LPCWAVEFORMATEX, DWORD_PTR, DWORD_PTR, DWORD);
	MMRESULT(WINAPI *in_prepare)(HWAVEIN, LPWAVEHDR, UINT);
	MMRESULT(WINAPI *in_add)(HWAVEIN, LPWAVEHDR, UINT);
	MMRESULT(WINAPI *in_start)(HWAVEIN);
	UINT(WINAPI *in_devs)(void);
	MMRESULT(WINAPI *in_caps)(UINT_PTR, LPWAVEINCAPSA, UINT);
	MMRESULT(WINAPI *out_open)(LPHWAVEOUT, UINT, LPCWAVEFORMATEX, DWORD_PTR, DWORD_PTR, DWORD);
	MMRESULT(WINAPI *out_prepare)(HWAVEOUT, LPWAVEHDR, UINT);
	MMRESULT(WINAPI *out_write)(HWAVEOUT, LPWAVEHDR, UINT);
} w;

static int enabled = 1, volume_pct = 100;
static char mic_part[64];
static volatile LONG key_down;		// +mw2coop_talk
static volatile LONG talk_until;		// GetTickCount: the tail after the key is let go
static volatile LONG qa_tone_until;		// QA: a tone in place of the microphone until then
static volatile LONG qa_tone_hz = 440;
static volatile LONG qa_listen_hz = 440; // QA: the frequency the partner's frames are measured at (mw2coop_voice_qa listen <hz>; not this PC's own tone)
static volatile LONG mic_open, out_open_ok;
static uint16_t seq_out;

// ---- IMA ADPCM
static const int steps[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209,
			      230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
			      2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
			      18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int index_step[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

static void adpcm_encode(const short *in, uint8_t *out, int *pred, int *idx)
{
	int p = *pred, ix = *idx;
	for (int i = 0; i < FRAME; i++) {
		int diff = in[i] - p, code = 0, step = steps[ix], vd = step >> 3;
		if (diff < 0)
			code = 8, diff = -diff;
		if (diff >= step)
			code |= 4, diff -= step, vd += step;
		step >>= 1;
		if (diff >= step)
			code |= 2, diff -= step, vd += step;
		step >>= 1;
		if (diff >= step)
			code |= 1, vd += step;
		p += (code & 8) ? -vd : vd;
		p = p > 32767 ? 32767 : p < -32768 ? -32768 : p;
		ix += index_step[code];
		ix = ix < 0 ? 0 : ix > 88 ? 88 : ix;
		if (i & 1)
			out[i >> 1] |= (uint8_t)(code << 4);
		else
			out[i >> 1] = (uint8_t)code;
	}
	*pred = p, *idx = ix;
}

static void adpcm_decode(const uint8_t *in, short *out, int p, int ix)
{
	for (int i = 0; i < FRAME; i++) {
		const int code = (i & 1) ? in[i >> 1] >> 4 : in[i >> 1] & 15, step = steps[ix];
		int vd = step >> 3;
		if (code & 4)
			vd += step;
		if (code & 2)
			vd += step >> 1;
		if (code & 1)
			vd += step >> 2;
		p += (code & 8) ? -vd : vd;
		p = p > 32767 ? 32767 : p < -32768 ? -32768 : p;
		ix += index_step[code];
		ix = ix < 0 ? 0 : ix > 88 ? 88 : ix;
		out[i] = (short)p;
	}
}

// ---- send

static int session_on(void)
{
	char coop[8] = "";
	game_dvar_raw("coop", coop, sizeof(coop));
	return enabled && coop[0] == '1' && net_partner_known() && !native_split_active();
}

static void send_frame(const short *pcm, int last)
{
	static int pred, idx;
	uint8_t pkt[TAG_LEN + HDR + CODED];
	memcpy(pkt, TAG, TAG_LEN);
	uint8_t *h = pkt + TAG_LEN;
	const uint16_t s = seq_out++;
	const uint32_t ms = GetTickCount();
	h[0] = 1, h[1] = (uint8_t)(last ? 1 : 0);
	memcpy(h + 2, &s, 2);
	memcpy(h + 4, &ms, 4);
	const int16_t p16 = (int16_t)pred;
	memcpy(h + 8, &p16, 2);
	h[10] = (uint8_t)idx, h[11] = 0;
	adpcm_encode(pcm, h + HDR, &pred, &idx);
	net_send_mod_bin(pkt, (int)sizeof(pkt));
}

static void tone_frame(short *pcm)
{
	static double phase;
	const double step = 2.0 * 3.14159265358979 * (double)qa_tone_hz / RATE;
	for (int i = 0; i < FRAME; i++) {
		pcm[i] = (short)(8000.0 * sin(phase)); // (about -12 dBFS)
		phase += step;
	}
}

static DWORD WINAPI capture_thread(LPVOID unused)
{
	(void)unused;
	HANDLE done = CreateEventA(NULL, FALSE, FALSE, NULL);
	HWAVEIN in = NULL;
	WAVEHDR hdr[IN_BUFS];
	static short bufs[IN_BUFS][FRAME];
	const WAVEFORMATEX fmt = {WAVE_FORMAT_PCM, 1, RATE, RATE * 2, 2, 16, 0};
	int was_talking = 0, sent = 0;
	DWORD stat_at = GetTickCount();
	for (;;) {
		const DWORD now = GetTickCount();
		const int tone = (LONG)(now - (DWORD)qa_tone_until) < 0;
		const int want = (key_down || (LONG)(now - (DWORD)talk_until) < 0) && session_on();
		if (tone && session_on()) { // QA: a frame of tone every 40 ms, no microphone
			short pcm[FRAME];
			tone_frame(pcm);
			send_frame(pcm, 0);
			sent++;
			Sleep(40);
		} else if (want && !mic_open) { // the microphone opens at the first press and stays open
			UINT dev = WAVE_MAPPER;
			if (mic_part[0] && w.in_devs && w.in_caps)
				for (UINT d = 0; d < w.in_devs(); d++) {
					WAVEINCAPSA caps;
					if (w.in_caps(d, &caps, sizeof(caps)) == MMSYSERR_NOERROR && strstr(caps.szPname, mic_part))
						dev = d;
				}
			if (w.in_open && w.in_open(&in, dev, &fmt, (DWORD_PTR)done, 0, CALLBACK_EVENT) == MMSYSERR_NOERROR) {
				for (int i = 0; i < IN_BUFS; i++) {
					memset(&hdr[i], 0, sizeof(hdr[i]));
					hdr[i].lpData = (LPSTR)bufs[i];
					hdr[i].dwBufferLength = sizeof(bufs[i]);
					w.in_prepare(in, &hdr[i], sizeof(hdr[i]));
					w.in_add(in, &hdr[i], sizeof(hdr[i]));
				}
				w.in_start(in);
				InterlockedExchange(&mic_open, 1);
				log_printf("voice: the microphone is open (%s)", dev == WAVE_MAPPER ? "Windows' default" : mic_part);
			} else {
				log_printf("voice: no microphone could be opened: no voice from this PC");
				InterlockedExchange(&key_down, 0);
				Sleep(2000);
			}
		} else if (mic_open) {
			WaitForSingleObject(done, 100);
			for (int i = 0; i < IN_BUFS; i++)
				if (hdr[i].dwFlags & WHDR_DONE) {
					const int talking = (key_down || (LONG)(GetTickCount() - (DWORD)talk_until) < 0) && session_on();
					if (talking) {
						send_frame((const short *)hdr[i].lpData, 0);
						sent++;
					} else if (was_talking) // the end of a burst: the partner's buffer plays out
						send_frame((const short *)hdr[i].lpData, 1);
					was_talking = talking;
					hdr[i].dwFlags &= ~WHDR_DONE;
					w.in_add(in, &hdr[i], sizeof(hdr[i]));
				}
		} else
			Sleep(50);
		if (GetTickCount() - stat_at >= 5000) {
			if (sent)
				log_printf("voice: %d frame(s) sent in 5 s (%d bytes/s)", sent, sent * (int)(TAG_LEN + HDR + CODED) / 5);
			sent = 0;
			stat_at = GetTickCount();
		}
	}
	return 0;
}

// ---- receive and play

static CRITICAL_SECTION jlock;
static short jitter[JITTER_MAX][FRAME];
static int j_head, j_count, j_started;
static int rx_frames, rx_lost, underruns, last_seq = -1;
static double g_tone, g_total; // QA: a Goertzel filter at the tone's frequency over the frames that came, and their energy
static volatile LONG talk_note_at;

void voice_on_packet(const char *buf, int len)
{
	if (len != (int)(TAG_LEN + HDR + CODED) || !enabled)
		return;
	const uint8_t *h = (const uint8_t *)buf + TAG_LEN;
	uint16_t s;
	int16_t p16;
	memcpy(&s, h + 2, 2);
	memcpy(&p16, h + 8, 2);
	if (h[0] != 1 || h[10] > 88)
		return;
	short pcm[FRAME];
	adpcm_decode(h + HDR, pcm, p16, h[10]);
	EnterCriticalSection(&jlock);
	if (last_seq >= 0) {
		const int gap = (uint16_t)(s - (uint16_t)last_seq) - 1;
		if (gap > 0 && gap < 50)
			rx_lost += gap;
	}
	last_seq = s;
	rx_frames++;
	if (j_count == JITTER_MAX) // full: the oldest goes
		j_head = (j_head + 1) % JITTER_MAX, j_count--;
	memcpy(jitter[(j_head + j_count) % JITTER_MAX], pcm, sizeof(pcm));
	j_count++;
	{ // QA: the tone's share of the frame's energy (Goertzel at the asked frequency)
		const double k = 2.0 * cos(2.0 * 3.14159265358979 * (double)qa_listen_hz / RATE);
		double s1 = 0, s2 = 0, e = 0;
		for (int i = 0; i < FRAME; i++) {
			const double x = pcm[i] / 32768.0, s0 = x + k * s1 - s2;
			s2 = s1, s1 = s0, e += x * x;
		}
		g_tone += (s1 * s1 + s2 * s2 - k * s1 * s2) * 2.0 / FRAME;
		g_total += e;
	}
	LeaveCriticalSection(&jlock);
	const DWORD now = GetTickCount();
	if (now - (DWORD)talk_note_at > 3000) // the start of his talk (a pause of 3 s before it): one line
		game_cbuf_addtext("mw2coop_voice_note\n");
	InterlockedExchange(&talk_note_at, (LONG)now);
}

static DWORD WINAPI playback_thread(LPVOID unused)
{
	(void)unused;
	HANDLE done = CreateEventA(NULL, FALSE, FALSE, NULL);
	HWAVEOUT out = NULL;
	const WAVEFORMATEX fmt = {WAVE_FORMAT_PCM, 1, RATE, RATE * 2, 2, 16, 0};
	if (!w.out_open || w.out_open(&out, WAVE_MAPPER, &fmt, (DWORD_PTR)done, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
		log_printf("voice: no sound output could be opened: the partner's voice is not played");
		return 0;
	}
	InterlockedExchange(&out_open_ok, 1);
	static short bufs[OUT_BUFS][FRAME];
	WAVEHDR hdr[OUT_BUFS];
	for (int i = 0; i < OUT_BUFS; i++) {
		memset(&hdr[i], 0, sizeof(hdr[i]));
		hdr[i].lpData = (LPSTR)bufs[i];
		hdr[i].dwBufferLength = sizeof(bufs[i]);
		w.out_prepare(out, &hdr[i], sizeof(hdr[i]));
		w.out_write(out, &hdr[i], sizeof(hdr[i])); // (silence first: the ring runs from now on)
	}
	DWORD stat_at = GetTickCount();
	for (;;) {
		WaitForSingleObject(done, 100);
		for (int i = 0; i < OUT_BUFS; i++) {
			if (!(hdr[i].dwFlags & WHDR_DONE))
				continue;
			short *b = bufs[i];
			EnterCriticalSection(&jlock);
			if (!j_started && j_count >= 3)
				j_started = 1;
			if (j_started && j_count > 0) {
				const int gain = volume_pct;
				const short *src = jitter[j_head];
				for (int n = 0; n < FRAME; n++) {
					const int v = src[n] * gain / 100;
					b[n] = (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
				}
				j_head = (j_head + 1) % JITTER_MAX, j_count--;
			} else {
				if (j_started)
					underruns++;
				j_started = j_count >= 3;
				memset(b, 0, FRAME * sizeof(short));
			}
			LeaveCriticalSection(&jlock);
			hdr[i].dwFlags &= ~WHDR_DONE;
			w.out_write(out, &hdr[i], sizeof(hdr[i]));
		}
		if (GetTickCount() - stat_at >= 5000) {
			EnterCriticalSection(&jlock);
			if (rx_frames)
				log_printf("voice: 5 s: %d frame(s) from the partner, %d lost, %d underrun(s); the share of %ld Hz in the energy %.0f%% (%.1f dBFS)", rx_frames,
					   rx_lost, underruns, qa_listen_hz, g_total > 0 ? 100.0 * g_tone / g_total : 0.0,
					   rx_frames ? 10.0 * log10(g_total / (rx_frames * (double)FRAME) + 1e-12) : -99.0);
			rx_frames = rx_lost = underruns = 0;
			g_tone = g_total = 0;
			LeaveCriticalSection(&jlock);
			stat_at = GetTickCount();
		}
	}
	return 0;
}

// ---- commands

static void talk_down_f(void)
{
	InterlockedExchange(&key_down, 1);
}

static void talk_up_f(void)
{
	InterlockedExchange(&key_down, 0);
	InterlockedExchange(&talk_until, (LONG)(GetTickCount() + TAIL_MS));
}

// the start of the partner's talk: one line in the game's message window (this PC only)
static void note_f(void)
{
	rejoin_info_t r;
	char name[40] = "";
	if (rejoin_info(&r))
		lstrcpynA(name, r.name, sizeof(name)); // (the partner's name as the session's launch noted it)
	const char *fmt = localize_text("MW2COOP_VOICE_TALKS_FMT");
	char text[96];
	const char *partner = localize_text("MW2COOP_VOICE_PARTNER"); // (no name noted: "Partner" in the game's language)
	snprintf(text, sizeof(text), fmt && ctexts_fmt_ok(fmt, "s") ? fmt : "%s talks", name[0] ? name : partner && partner[0] ? partner : "Partner");
	netwatch_say(text);
}

// QA: mw2coop_voice_qa tone <hz> <ms> (send a tone), mw2coop_voice_qa listen <hz> (measure the partner's frames at that frequency; 440 by default)
static void qa_f(void)
{
	if (game_cmd_argc() >= 3 && !strcmp(game_cmd_argv(1), "listen") && atoi(game_cmd_argv(2)) > 0) {
		InterlockedExchange(&qa_listen_hz, atoi(game_cmd_argv(2)));
		log_printf("voice: QA: the partner's frames are measured at %ld Hz", qa_listen_hz);
	}
	if (game_cmd_argc() >= 4 && !strcmp(game_cmd_argv(1), "tone")) {
		InterlockedExchange(&qa_tone_hz, atoi(game_cmd_argv(2)) > 0 ? atoi(game_cmd_argv(2)) : 440);
		InterlockedExchange(&qa_tone_until, (LONG)(GetTickCount() + (DWORD)atoi(game_cmd_argv(3))));
		log_printf("voice: QA: a %ld Hz tone is sent for %s ms (session %s, output %s)", qa_tone_hz, game_cmd_argv(3), session_on() ? "on" : "off", out_open_ok ? "open" : "closed");
	}
}

// the key: [voice] talk_key bound once to +mw2coop_talk where the player's config does not bind it already
void voice_menu_tick(void)
{
	static int done;
	if (done || !enabled)
		return;
	done = 1;
	char key[16] = "";
	GetPrivateProfileStringA("voice", "talk_key", "CAPSLOCK", key, sizeof(key), settings_ini_path());
	for (char *c = key; *c; c++)
		if (!((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_')) {
			key[0] = 0;
			break;
		}
	if (!key[0])
		return;
	char path[MAX_PATH];
	DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
	while (n > 0 && path[n - 1] != '\\')
		n--;
	path[n] = 0;
	lstrcatA(path, "players\\config.cfg");
	FILE *f = fopen(path, "rb");
	char line[256], want[40];
	int bound_talk = 0, key_taken = 0;
	snprintf(want, sizeof(want), "bind %s ", key);
	if (f) {
		while (fgets(line, sizeof(line), f)) {
			if (strstr(line, "+mw2coop_talk"))
				bound_talk = 1;
			else if (_strnicmp(line, want, strlen(want)) == 0)
				key_taken = 1;
		}
		fclose(f);
	}
	if (bound_talk)
		return;
	if (key_taken) {
		log_printf("voice: %s is bound to something else: no key for the voice (bind one to +mw2coop_talk)", key);
		return;
	}
	char cmd[64];
	snprintf(cmd, sizeof(cmd), "bind %s +mw2coop_talk\n", key);
	game_cbuf_addtext(cmd);
	log_printf("voice: %s held talks to the partner (bind %s +mw2coop_talk)", key, key);
}

static cmd_function_t commands[] = {
	{NULL, "+mw2coop_talk", NULL, NULL, talk_down_f CMD_NODE_FLAG},
	{NULL, "-mw2coop_talk", NULL, NULL, talk_up_f CMD_NODE_FLAG},
	{NULL, "mw2coop_voice_note", NULL, NULL, note_f CMD_NODE_FLAG},
	{NULL, "mw2coop_voice_qa", NULL, NULL, qa_f CMD_NODE_FLAG},
};

void voice_install(void)
{
	const char *ini = settings_ini_path();
	enabled = GetPrivateProfileIntA("voice", "enabled", 1, ini) != 0;
	volume_pct = GetPrivateProfileIntA("voice", "volume_pct", 100, ini);
	volume_pct = volume_pct < 0 ? 0 : volume_pct > 200 ? 200 : volume_pct;
	GetPrivateProfileStringA("voice", "mic", "", mic_part, sizeof(mic_part), ini);
	char *v = getenv("MW2COOP_VOICE_VOLUME"); // QA: a silent PC
	if (v)
		volume_pct = atoi(v);
	for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
		game_add_command(&commands[i]);
	if (!enabled) {
		log_printf("voice: off ([voice] enabled=0)");
		return;
	}
	wchar_t sys[MAX_PATH];
	UINT n = GetSystemDirectoryW(sys, MAX_PATH);
	if (!n || n > MAX_PATH - 12)
		return;
	lstrcatW(sys, L"\\winmm.dll");
	HMODULE m = LoadLibraryW(sys);
	if (!m)
		return;
	*(FARPROC *)&w.in_open = GetProcAddress(m, "waveInOpen");
	*(FARPROC *)&w.in_prepare = GetProcAddress(m, "waveInPrepareHeader");
	*(FARPROC *)&w.in_add = GetProcAddress(m, "waveInAddBuffer");
	*(FARPROC *)&w.in_start = GetProcAddress(m, "waveInStart");
	*(FARPROC *)&w.in_devs = GetProcAddress(m, "waveInGetNumDevs");
	*(FARPROC *)&w.in_caps = GetProcAddress(m, "waveInGetDevCapsA");
	*(FARPROC *)&w.out_open = GetProcAddress(m, "waveOutOpen");
	*(FARPROC *)&w.out_prepare = GetProcAddress(m, "waveOutPrepareHeader");
	*(FARPROC *)&w.out_write = GetProcAddress(m, "waveOutWrite");
	InitializeCriticalSection(&jlock);
	CloseHandle(CreateThread(NULL, 0, capture_thread, NULL, 0, NULL));
	CloseHandle(CreateThread(NULL, 0, playback_thread, NULL, 0, NULL));
	log_printf("voice: push-to-talk ready (volume %d%%%s%s)", volume_pct, mic_part[0] ? ", microphone " : "", mic_part);
}
