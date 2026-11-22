#ifndef ROLLTUI_C_WIDGET_DIALOG_H
#define ROLLTUI_C_WIDGET_DIALOG_H
/* INTERNAL: the public declarations of this module live in `rolltui/rolltui.h` (a dialog, `rolltui_windows_ask` and
 * `rolltui_windows_answer`). What is below is the `dialog` kind's factory, which the library's kinds register.
 *
 * rolltui/c/rolltui_widget_dialog.h — A QUESTION THE APP ASKS: a popup the window table opens, draws and closes.
 */
#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_widgets.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The `dialog:<id>` window's widget: it draws and answers the dialog the table holds under `id`. */
RolltuiWidget rolltui_dialog_widget_factory(void* ctx, RolltuiWindows* w, const char* content, size_t len);

#ifdef __cplusplus
}
#endif
#endif
