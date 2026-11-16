#ifndef ROLLTUI_C_FEEDBACK_H
#define ROLLTUI_C_FEEDBACK_H
/* INTERNAL: the public declarations of this module live in `rolltui/rolltui.h` (the gauge, the spinner, and a window table's
 * messages). What is below is the library's own: the message log a `RolltuiWindows` owns, which its `messages` kind and its toasts
 * read. */
/*
 * rolltui/c/rolltui_feedback.h — WHAT THE APP TELLS THE PERSON: how far something has got, and messages.
 *
 * The log is a ring of the newest ROLLTUI_MESSAGE_HISTORY entries, each a level, a time and its own text. The toasts are separate
 * copies, at most ROLLTUI_TOASTS_MAX, because a toast outlives the log entry it came from once the log has wrapped, and it carries
 * what only a toast has: where it was last drawn (a press dismisses it) and whether it was dismissed.
 */
#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROLLTUI_TOASTS_MAX 8

typedef struct RolltuiMessage {
  int level;
  unsigned long long at_ms;
  RolltuiStr text;
} RolltuiMessage;

typedef struct RolltuiToast {
  int level;
  unsigned long long at_ms;
  RolltuiStr text;
  RolltuiRect drawn; /* where the last draw put it; w 0 when it was not drawn */
} RolltuiToast;

typedef struct RolltuiMessageLog {
  RolltuiMessage* v; /* a ring: `head` is the oldest */
  size_t n, cap, head;
  RolltuiToast toasts[ROLLTUI_TOASTS_MAX]; /* the oldest first */
  size_t toast_n;
  RolltuiDrawScratch* draw;
  RolltuiUnicodeScratch* u;
  RolltuiWrapLines* wrap;
} RolltuiMessageLog;

void rolltui_message_log_init(RolltuiMessageLog* log);
void rolltui_message_log_release(RolltuiMessageLog* log);
void rolltui_message_log_post(RolltuiMessageLog* log, int level, const char* text, size_t len, unsigned long long now_ms);
size_t rolltui_message_log_count(const RolltuiMessageLog* log);
const RolltuiMessage* rolltui_message_log_at(const RolltuiMessageLog* log, size_t i);
/* How long a message of `level` shows as a toast. */
unsigned long long rolltui_toast_ms(int level);
/* The role and the glyph a level is drawn in. The glyph is one cell wherever it is drawn. */
unsigned char rolltui_message_role(const RolltuiBuiltinRoles* roles, int level);
const char* rolltui_message_glyph(int level, size_t* len);
int rolltui_message_log_draw_toasts(RolltuiMessageLog* log, RolltuiFrame* f, RolltuiRect screen, const RolltuiStyle* styles,
                                    const RolltuiBuiltinRoles* roles, unsigned long long now_ms, int ambiguous_wide, int still);
int rolltui_message_log_toast_event(RolltuiMessageLog* log, const RolltuiEvent* e);
void rolltui_message_log_dismiss(RolltuiMessageLog* log);

/* The gauge, drawn in roles handed in rather than a table's (the window kind's and the public call's shared body). */
int rolltui_gauge_draw_roles(RolltuiFrame* f, RolltuiDrawScratch* draw, int x, int y, int width, const RolltuiGauge* g,
                             const RolltuiStyle* styles, const RolltuiBuiltinRoles* roles, int ambiguous_wide);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_FEEDBACK_H */
