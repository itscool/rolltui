/* hello_test.c: an end-to-end test of examples/hello.c with no terminal. It runs the same app, sends it keys and checks the
 * screen as text. docs/guide.md quotes this file. */
#define _POSIX_C_SOURCE 200809L /* nanosleep */
#define HELLO_NO_MAIN
#include "rolltui/examples/hello.c"

#include <time.h>

#include "testkit/testkit.h"

/* Whether `text` appears anywhere on the screen. */
static int shows(RolltuiRun* run, const char* text) {
  RolltuiStr screen;
  int found;
  memset(&screen, 0, sizeof screen);
  rolltui_run_screen_text(run, &screen);
  found = screen.p && strstr(screen.p, text) != NULL;
  rolltui_str_free(&screen);
  return found;
}

/* Keep stepping until `text` appears, for at most `ms`. Some things, like a message sliding in, take a few frames. */
static int shows_within(RolltuiRun* run, const char* text, int ms) {
  const struct timespec frame = {0, 20 * 1000000};
  int waited;
  for (waited = 0; !shows(run, text) && waited < ms; waited += 20) {
    nanosleep(&frame, NULL);
    rolltui_run_step(run);
  }
  return shows(run, text);
}

/* Press a key and draw the resulting frame. */
static int press(RolltuiRun* run, unsigned char key, unsigned int ch) {
  RolltuiEvent e;
  memset(&e, 0, sizeof e);
  e.kind = ROLLTUI_EVENT_KEY;
  e.key.key = key;
  e.key.ch = ch;
  rolltui_run_inject(run, &e);
  return rolltui_run_step(run);
}

int main(void) {
  Hello h;
  RolltuiRunApp app;
  RolltuiRun* run;
  int result = -1;
  hello_init(&h);
  app = hello_app(&h);
  run = rolltui_run_begin_headless(60, 12, &app, &result);
  testkit_check(run != NULL, "hello runs with no terminal");
  if (run) {
    rolltui_run_step(run);
    testkit_check(shows(run, "apple") && shows(run, "Enter chooses"), "its first frame shows the list and the page");
    press(run, ROLLTUI_KEY_DOWN, 0);
    press(run, ROLLTUI_KEY_ENTER, 0);
    testkit_check(shows_within(run, "You chose banana.", 1000), "Down and Enter choose the second fruit, and the message says so");
    testkit_check(press(run, ROLLTUI_KEY_CHAR, 'q') == ROLLTUI_RUN_STEP_STOPPED, "q quits");
    testkit_check(rolltui_run_finish(run) == ROLLTUI_RUN_STOPPED, "and the run finishes");
  }
  hello_free(&h);
  return testkit_report("hello_test");
}
