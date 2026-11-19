#pragma once

// QA: log fatal exceptions (faulting RVA, registers, return addresses) to mw2coop.log.
void crash_install(void);
// The QA's stack dump request (mw2coop\dumpstacks -> mw2coop\stacks.txt) watched from now on (crash_install starts it too; once only):
// called before the wait for the game window, so a start that hangs before it can be asked for its stacks.
void crash_stack_watch(void);
