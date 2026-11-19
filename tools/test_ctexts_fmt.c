// Unit test of ctexts_fmt_ok (src/ctexts.c): a translated text used as a printf format keeps the built-in one's conversions.
// Build and run: tools/test_ctexts_fmt.sh
#include <stdio.h>

#include "../src/ctexts.h"

static int fails;

static void expect(const char *fmt, const char *want, int ok)
{
	const int got = ctexts_fmt_ok(fmt, want);
	printf("%s \"%s\" for \"%s\" -> %d\n", got == ok ? "ok  " : "FAIL", fmt, want, got);
	if (got != ok)
		fails++;
}

int main(void)
{
	expect("%s", "s", 1);
	expect("Mission %s with %s and %s", "sss", 1);
	expect("only %s", "sss", 1);       // a value left out (the first ones): fine
	expect("no values", "sss", 1);
	expect("%d%%", "d", 1);            // a literal percent
	expect("pad %d", "d", 1);
	expect("%s %s", "s", 0);           // one more than the code passes
	expect("%d", "s", 0);              // another type
	expect("%s %d", "ss", 0);
	expect("%*s", "s", 0);             // a width taken from the arguments
	expect("%n", "s", 0);
	expect("%5.2s", "s", 1);           // flags and width are fine
	expect("%ls", "s", 0);          // a wide string: not the char string the code passes
	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails != 0;
}
