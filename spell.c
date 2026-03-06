#include "prose_code.h"

/* GUIDs defined as static const — NOT via INITGUID to avoid
 * multiple-definition linker errors across translation units. */
static const GUID CLSID_SpellCheckerFactory =
    {0x7AB36653, 0x1796, 0x484B, {0xBD, 0xFA, 0xE7, 0x4F, 0x1D, 0xB7, 0xC1, 0xDC}};
static const GUID IID_ISpellCheckerFactory =
    {0x8E018A9D, 0x2415, 0x4677, {0xBF, 0x08, 0x79, 0x4E, 0xA6, 0x1F, 0x94, 0xBB}};

/* ── Spell cache (file-local) ── */
static SpellCacheNode *g_spell_cache[SPELL_CACHE_BUCKETS];
static int             g_spell_cache_count = 0;

ISpellChecker *g_spell_checker = NULL;
int            g_spell_loaded  = 0;

static unsigned int spell_hash(const wchar_t *word) {
    unsigned int h = 5381;
    while (*word) {
        h = ((h << 5) + h) + towlower(*word);
        word++;
    }
    return h;
}

static int spell_cache_lookup(const wchar_t *lower, int len) {
    unsigned int h = spell_hash(lower) & SPELL_CACHE_MASK;
    for (SpellCacheNode *n = g_spell_cache[h]; n; n = n->next) {
        if (wcsncmp(n->word, lower, len) == 0 && n->word[len] == 0)
            return n->correct;
    }
    return -1;
}

static void spell_cache_insert(const wchar_t *lower, int len, int correct) {
    if (g_spell_cache_count >= SPELL_CACHE_MAX) {
        int evicted = 0;
        for (int i = 0; i < SPELL_CACHE_BUCKETS; i += 2) {
            SpellCacheNode *n = g_spell_cache[i];
            while (n) {
                SpellCacheNode *next = n->next;
                free(n->word);
                free(n);
                evicted++;
                n = next;
            }
            g_spell_cache[i] = NULL;
        }
        g_spell_cache_count -= evicted;
    }
    unsigned int h = spell_hash(lower) & SPELL_CACHE_MASK;
    SpellCacheNode *n = (SpellCacheNode *)malloc(sizeof(SpellCacheNode));
    if (!n) return;
    n->word = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (!n->word) { free(n); return; }
    memcpy(n->word, lower, len * sizeof(wchar_t));
    n->word[len] = 0;
    n->correct = correct;
    n->next = g_spell_cache[h];
    g_spell_cache[h] = n;
    g_spell_cache_count++;
}

int spell_check(const wchar_t *word, int len) {
    if (len <= 1) return 1;
    if (!g_spell_loaded) return 1;
    if (!g_editor.spellcheck_enabled) return 1;

    for (int i = 0; i < len; i++) {
        if (iswdigit(word[i])) return 1;
    }

    wchar_t lower[64];
    if (len >= 64) return 1;
    for (int i = 0; i < len; i++) lower[i] = towlower(word[i]);
    lower[len] = 0;

    int cached = spell_cache_lookup(lower, len);
    if (cached >= 0) return cached;

    if (g_spell_checker) {
        wchar_t tmp[64];
        memcpy(tmp, word, len * sizeof(wchar_t));
        tmp[len] = 0;

        IEnumSpellingError *errors = NULL;
        HRESULT hr = g_spell_checker->lpVtbl->Check(g_spell_checker, tmp, &errors);
        if (SUCCEEDED(hr) && errors) {
            ISpellingError *err = NULL;
            hr = errors->lpVtbl->Next(errors, &err);
            if (err) {
                err->lpVtbl->Release(err);
                errors->lpVtbl->Release(errors);
                spell_cache_insert(lower, len, 0);
                return 0;
            }
            errors->lpVtbl->Release(errors);
            spell_cache_insert(lower, len, 1);
            return 1;
        }
        if (errors) errors->lpVtbl->Release(errors);
    }

    return 1;
}

void spell_init(void) {
    memset(g_spell_cache, 0, sizeof(g_spell_cache));

    ISpellCheckerFactory *factory = NULL;
    HRESULT hr = CoCreateInstance(
        &CLSID_SpellCheckerFactory, NULL, CLSCTX_INPROC_SERVER,
        &IID_ISpellCheckerFactory, (void **)&factory);

    if (SUCCEEDED(hr) && factory) {
        BOOL supported = FALSE;
        factory->lpVtbl->IsSupported(factory, L"en-US", &supported);

        if (supported) {
            hr = factory->lpVtbl->CreateSpellChecker(factory, L"en-US", &g_spell_checker);
            if (SUCCEEDED(hr) && g_spell_checker) {
                g_spell_loaded = 1;
            }
        }
        factory->lpVtbl->Release(factory);
    }

    if (!g_spell_loaded) {
        g_spell_loaded = 0;
    }
}

void spell_cache_free(void) {
    for (int i = 0; i < SPELL_CACHE_BUCKETS; i++) {
        SpellCacheNode *n = g_spell_cache[i];
        while (n) {
            SpellCacheNode *next = n->next;
            free(n->word);
            free(n);
            n = next;
        }
        g_spell_cache[i] = NULL;
    }
    g_spell_cache_count = 0;
}
