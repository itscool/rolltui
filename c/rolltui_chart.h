#ifndef ROLLTUI_C_CHART_H
#define ROLLTUI_C_CHART_H
/* INTERNAL: the public declarations of this module live in `rolltui/rolltui.h` (a chart, its series, a `chart:` window). What is below
 * is the draw in roles handed in, which the chart kind calls.
 *
 * rolltui/c/rolltui_chart.h — numbers drawn.
 */
#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The roles a chart draws in, handed in as bytes: its range's labels, and each series' colour when it names none. */
typedef struct RolltuiChartRoles {
  unsigned char axis;
  unsigned char series[ROLLTUI_CHART_SERIES_MAX];
} RolltuiChartRoles;

void rolltui_chart_draw_roles(RolltuiChart* c, RolltuiFrame* f, RolltuiDrawScratch* draw, RolltuiRect area, const RolltuiStyle* styles,
                              const RolltuiChartRoles* roles, int ambiguous_wide);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_CHART_H */
