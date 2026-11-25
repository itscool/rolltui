/* rolltui/c/rolltui_texts.c — the words the library puts on screen, in the person's language. See rolltui_texts.h; the loader, the
 * template and the locale's language are in rolltui.h. */
#include "rolltui/c/rolltui_texts.h"

#include <stdlib.h>
#include <string.h>

#include "rolltui/c/rolltui_alloc.h"
#include "rolltui/c/rolltui_bindings.h"
#include "rolltui/c/rolltui_json.h"
#include "rolltui/c/rolltui_lifetime.h"
#include "rolltui/c/rolltui_map.h"
#include "rolltui/c/rolltui_str.h"

#define LIT(s) s, sizeof(s) - 1 /* a literal and its length, never counted by hand */

typedef struct Text {
  const char* id;
  const char* en;
} Text;

static const Text kTexts[ROLLTUI_TEXT_COUNT] = {
#define ROLLTUI_TEXT_ROW_(sym, id, en) {id, en},
    ROLLTUI_TEXT_LIST(ROLLTUI_TEXT_ROW_)
#undef ROLLTUI_TEXT_ROW_
};

/* THE PERSON'S LANGUAGE, one a process as the log is: every text a texts file gave, by id (OWNED RolltuiStr values), and the
 * library's own looked up once at load. Set before anything is drawn. */
static RolltuiMap g_named;
static const RolltuiStr* g_own[ROLLTUI_TEXT_COUNT];
static int g_on_shutdown; /* the table is freed by `rolltui_shutdown`, asked once a load until then */

void rolltui_texts_clear(void) {
  size_t i;
  for (i = 0; i < rolltui_map_count(&g_named); ++i) {
    RolltuiStr* s = (RolltuiStr*)rolltui_map_value_at(&g_named, i);
    rolltui_str_free(s);
    rolltui_mem_free(s);
  }
  rolltui_map_release(&g_named);
  memset(g_own, 0, sizeof g_own);
}

static void texts_at_shutdown(void) {
  rolltui_texts_clear();
  g_on_shutdown = 0;
}

int rolltui_texts_load(const char* json, size_t len, RolltuiStr* err) {
  RolltuiJsonValue* root;
  const RolltuiJsonValue* texts;
  size_t i, n;
  if (err) rolltui_str_clear(err);
  root = rolltui_json_parse(json ? json : "", json ? len : 0, err);
  if (!root) return 0;
  texts = rolltui_json_get(root, "texts", 5); /* a JSON null when there is none, never NULL */
  if (!rolltui_json_is_object(root) || !rolltui_json_is_object(texts)) {
    if (err) rolltui_str_set(err, LIT("a texts file is an object with \"texts\": { id: text }"));
    rolltui_json_free(root);
    return 0;
  }
  rolltui_texts_clear();
  if (!g_on_shutdown) {
    rolltui_on_shutdown(texts_at_shutdown);
    g_on_shutdown = 1;
  }
  n = rolltui_json_object_size(texts);
  for (i = 0; i < n; ++i) {
    size_t kl = 0, vl = 0;
    const char* k = rolltui_json_object_key_at(texts, i, &kl);
    const char* v = rolltui_json_as_string(rolltui_json_object_value_at(texts, i), NULL, 0, &vl);
    RolltuiStr* s;
    if (!k || !v || vl == 0) continue; /* not a string, or empty: the English stands */
    s = (RolltuiStr*)rolltui_mem_alloc(sizeof *s);
    memset(s, 0, sizeof *s);
    rolltui_str_set(s, v, vl);
    s = (RolltuiStr*)rolltui_map_put(&g_named, k, kl, s);
    if (s) { /* a key given twice: the later stands */
      rolltui_str_free(s);
      rolltui_mem_free(s);
    }
  }
  for (i = 0; i < ROLLTUI_TEXT_COUNT; ++i) g_own[i] = (const RolltuiStr*)rolltui_map_get(&g_named, kTexts[i].id, strlen(kTexts[i].id));
  rolltui_json_free(root);
  return 1;
}

const char* rolltui_text(int id, size_t* len) {
  if (id < 0 || id >= ROLLTUI_TEXT_COUNT) {
    if (len) *len = 0;
    return "";
  }
  if (g_own[id]) {
    if (len) *len = g_own[id]->n;
    return g_own[id]->p;
  }
  if (len) *len = strlen(kTexts[id].en);
  return kTexts[id].en;
}

const char* rolltui_text_named(const char* id, size_t len, size_t* out_len) {
  const RolltuiStr* s = rolltui_map_count(&g_named) ? (const RolltuiStr*)rolltui_map_get(&g_named, id, len) : NULL;
  if (out_len) *out_len = s ? s->n : 0;
  return s ? s->p : NULL;
}

void rolltui_text_format(RolltuiStr* out, int id, const char* a1, size_t n1, const char* a2, size_t n2) {
  size_t n = 0, i;
  const char* t = rolltui_text(id, &n);
  for (i = 0; i < n; ++i) {
    if (t[i] == '{' && i + 2 < n && (t[i + 1] == '1' || t[i + 1] == '2') && t[i + 2] == '}') {
      if (t[i + 1] == '1' && a1) rolltui_str_append(out, a1, n1);
      if (t[i + 1] == '2' && a2) rolltui_str_append(out, a2, n2);
      i += 2;
      continue;
    }
    rolltui_str_append(out, t + i, 1);
  }
}

/* A JSON string's quoting, for the template. */
static void quoted(RolltuiStr* out, const char* s, size_t n) {
  RolltuiJsonValue* v = rolltui_json_string(s, n);
  RolltuiStr q;
  memset(&q, 0, sizeof q);
  rolltui_json_dump(v, -1, &q); /* the dump REPLACES what it is handed: appended from a string of its own */
  rolltui_str_append_str(out, &q);
  rolltui_str_free(&q);
  rolltui_json_free(v);
}

void rolltui_texts_template(const RolltuiBindings* b, RolltuiStr* out) {
  size_t i;
  int first = 1;
  if (!out) return;
  rolltui_str_append(out, LIT("{\n  \"language\": \"\",\n  \"texts\": {\n"));
  for (i = 0; i < ROLLTUI_TEXT_COUNT; ++i) {
    if (!first) rolltui_str_append(out, LIT(",\n"));
    first = 0;
    rolltui_str_append(out, LIT("    "));
    quoted(out, kTexts[i].id, strlen(kTexts[i].id));
    rolltui_str_append(out, LIT(": "));
    quoted(out, kTexts[i].en, strlen(kTexts[i].en));
  }
  for (i = 0; b && i < rolltui_bindings_action_count(b); ++i) { /* every action the table declares, by its name */
    size_t al = 0, dl = 0;
    const char* a = rolltui_bindings_action_at(b, i, &al);
    const char* d = a ? rolltui_bindings_english(b, a, al, &dl) : NULL;
    if (!a || !d || dl == 0) continue;
    if (!first) rolltui_str_append(out, LIT(",\n"));
    first = 0;
    rolltui_str_append(out, LIT("    "));
    quoted(out, a, al);
    rolltui_str_append(out, LIT(": "));
    quoted(out, d, dl);
  }
  rolltui_str_append(out, LIT("\n  }\n}\n"));
}

size_t rolltui_locale_language(char* out, size_t cap) {
  static const char* const kOrder[] = {"LC_ALL", "LC_MESSAGES", "LANG"};
  const char* v = NULL;
  size_t i, n = 0;
  if (!out || cap == 0) return 0;
  for (i = 0; i < 3 && !v; ++i) {
    const char* e = getenv(kOrder[i]);
    if (e && *e) v = e;
  }
  /* the letters before the territory, the codeset or a modifier: "fr" of fr_CA.UTF-8; the POSIX locale is English */
  if (v && strcmp(v, "C") != 0 && strcmp(v, "POSIX") != 0 && strncmp(v, "C.", 2) != 0)
    while (v[n] && v[n] != '_' && v[n] != '.' && v[n] != '@' && v[n] != '-' && n + 1 < cap) {
      out[n] = (char)(v[n] >= 'A' && v[n] <= 'Z' ? v[n] - 'A' + 'a' : v[n]);
      ++n;
    }
  if (n == 0 && cap > 2) out[n++] = 'e', out[n++] = 'n';
  out[n] = 0;
  return n;
}
