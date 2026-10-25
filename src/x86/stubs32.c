// Parts of the 64-bit DLL that the 32-bit build does not have yet; each stub is replaced by the real thing as
// that part is ported (docs/32bit-plan.md).
#include "scrdebug.h"

void scrdebug_note_source(const char *ext, const char *text, int len)
{
	(void)ext;
	(void)text;
	(void)len;
}
