// See inisync.h.
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "inimerge.h"
#include "inisync.h"
#include "log.h"

// settings.ini is made at the start of the game, and kept up to date: a file that is not there is made from the mod's built-in template (the documented defaults,
// content/release/settings.ini); one that is there gets the keys and the comments of this version of the mod, and keeps what the player changed (inimerge.h). The
// file as it was goes to settings.ini.bak when the merge changes it. A file the mod cannot read as text (UTF-16) or cannot write is left alone.
void ini_sync_file(const char *path, const char *tmpl)
{
	char *old = NULL;
	DWORD old_n = 0;
	int existed = 0;
	HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h != INVALID_HANDLE_VALUE) {
		existed = 1;
		DWORD size = GetFileSize(h, NULL);
		if (size == INVALID_FILE_SIZE || size > (1u << 20)) {
			CloseHandle(h);
			log_printf("settings: settings.ini is too big to bring up to date, left alone");
			return;
		}
		old = (char *)malloc((size_t)size + 1);
		if (!old || !ReadFile(h, old, size, &old_n, NULL) || old_n != size) {
			CloseHandle(h);
			free(old);
			log_printf("settings: settings.ini could not be read, left alone");
			return;
		}
		old[old_n] = 0;
		CloseHandle(h);
	} else if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) {
		log_printf("settings: settings.ini could not be opened (error %lu), left alone", GetLastError());
		return;
	}
	const char *text = old ? old : "";
	if (old_n >= 2 && (uint8_t)old[0] == 0xff && (uint8_t)old[1] == 0xfe) {
		log_printf("settings: settings.ini is saved as UTF-16: left alone (save it as UTF-8 or ANSI to let the mod bring it up to date)");
		free(old);
		return;
	}
	int bom = old_n >= 3 && (uint8_t)old[0] == 0xef && (uint8_t)old[1] == 0xbb && (uint8_t)old[2] == 0xbf;
	if (bom)
		text += 3; // the byte order mark of a UTF-8 file: the merged file has none
	IniMergeStats st;
	char *merged = ini_merge(tmpl, text, &st);
	if (!merged) {
		free(old);
		return;
	}
	// the same text as the file has now? (line ends and the blank lines at the end do not count; a file with a byte order mark is rewritten once)
	int same = existed && !bom;
	if (same) {
		size_t mn = strlen(merged), on = strlen(text), i = 0, j = 0;
		while (mn && merged[mn - 1] == '\n')
			mn--;
		while (on && (text[on - 1] == '\n' || text[on - 1] == '\r'))
			on--;
		while (same && (i < on || j < mn)) {
			if (i < on && text[i] == '\r') {
				i++;
				continue;
			}
			if (i >= on || j >= mn || text[i] != merged[j])
				same = 0;
			else
				i++, j++;
		}
	}
	if (!same) {
		// CRLF line ends, as the file the mod shipped had
		size_t lines = 0;
		for (size_t i = 0; merged[i]; i++)
			lines += merged[i] == '\n';
		char *crlf = (char *)malloc(strlen(merged) + lines + 1);
		char tmp[MAX_PATH + 8], bak[MAX_PATH + 8];
		if (crlf) {
			char *w = crlf;
			for (const char *r = merged; *r; r++) {
				if (*r == '\n')
					*w++ = '\r';
				*w++ = *r;
			}
			*w = 0;
			snprintf(tmp, sizeof(tmp), "%s.tmp", path);
			snprintf(bak, sizeof(bak), "%s.bak", path);
			char dir[MAX_PATH];
			lstrcpynA(dir, path, sizeof(dir));
			char *slash = strrchr(dir, '\\');
			if (slash) {
				*slash = 0;
				CreateDirectoryA(dir, NULL); // (the mod's folder is there once it is installed; a copy of only the DLL gets it now)
			}
			HANDLE f = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			DWORD wrote = 0;
			int ok = f != INVALID_HANDLE_VALUE && WriteFile(f, crlf, (DWORD)strlen(crlf), &wrote, NULL) && wrote == strlen(crlf);
			if (f != INVALID_HANDLE_VALUE)
				CloseHandle(f);
			if (ok && existed)
				CopyFileA(path, bak, FALSE);
			if (ok && MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
				if (existed)
					log_printf("settings: settings.ini brought up to date (%d of your values kept, %d keys added, %d other keys kept; the old file is settings.ini.bak)",
						   st.player_values_kept, st.template_keys_added, st.unknown_kept);
				else
					log_printf("settings: settings.ini made from the built-in defaults");
			} else {
				DeleteFileA(tmp);
				log_printf("settings: settings.ini could not be written (error %lu), the file as it is stays in use", GetLastError());
			}
			free(crlf);
		}
	}
	free(merged);
	free(old);
}
