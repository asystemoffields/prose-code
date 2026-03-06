#include "prose_code.h"

COLORREF token_color(SynToken t) {
    switch (t) {
        case TOK_KEYWORD:       return CLR_MAUVE;
        case TOK_STRING:        return CLR_GREEN;
        case TOK_COMMENT:       return CLR_OVERLAY0;
        case TOK_NUMBER:        return CLR_PEACH;
        case TOK_PREPROCESSOR:  return CLR_PINK;
        case TOK_TYPE:          return CLR_YELLOW;
        case TOK_FUNCTION:      return CLR_BLUE;
        case TOK_OPERATOR:      return CLR_SKY;
        case TOK_MD_HEADING:    return CLR_MAUVE;
        case TOK_MD_BOLD:       return CLR_PEACH;
        case TOK_MD_ITALIC:     return CLR_FLAMINGO;
        case TOK_MD_CODE:       return CLR_GREEN;
        case TOK_MD_LINK:       return CLR_BLUE;
        case TOK_MD_BLOCKQUOTE: return CLR_OVERLAY0;
        case TOK_MD_LIST:       return CLR_TEAL;
        case TOK_MISSPELLED:    return CLR_MISSPELLED;
        default:                return CLR_TEXT;
    }
}

static const wchar_t *c_keywords[] = {
    L"auto",L"break",L"case",L"char",L"const",L"continue",L"default",
    L"do",L"double",L"else",L"enum",L"extern",L"float",L"for",L"goto",
    L"if",L"inline",L"int",L"long",L"register",L"restrict",L"return",
    L"short",L"signed",L"sizeof",L"static",L"struct",L"switch",
    L"typedef",L"union",L"unsigned",L"void",L"volatile",L"while",
    L"bool",L"true",L"false",L"NULL",L"nullptr",
    L"class",L"namespace",L"template",L"typename",L"virtual",L"override",
    L"public",L"private",L"protected",L"new",L"delete",L"this",
    L"try",L"catch",L"throw",L"using",L"const_cast",L"dynamic_cast",
    L"static_cast",L"reinterpret_cast",L"noexcept",L"constexpr",
    L"decltype",L"explicit",L"friend",L"mutable",
    L"def",L"import",L"from",L"as",L"pass",L"lambda",
    L"with",L"yield",L"assert",L"raise",L"except",L"finally",
    L"global",L"nonlocal",L"del",L"in",L"not",L"and",L"or",L"is",
    L"None",L"True",L"False",L"self",L"elif",L"async",L"await",
    L"function",L"var",L"let",
    L"export",L"extends",L"implements",
    L"interface",L"type",L"declare",L"module",L"require",
    L"undefined",L"NaN",L"Infinity",L"arguments",L"of",
    L"fn",L"mut",L"pub",L"crate",L"mod",L"use",L"impl",
    L"trait",L"where",L"loop",L"match",L"ref",L"move",L"unsafe",
    L"dyn",L"Box",L"Vec",L"String",L"Option",L"Result",L"Some",
    L"Ok",L"Err",L"Self",L"super",
    NULL
};

static KwNode *g_kw_table[KW_HASH_BUCKETS];
static KwNode  g_kw_pool[256];

static unsigned int kw_hash(const wchar_t *word, int len) {
    unsigned int h = 5381;
    for (int i = 0; i < len; i++) h = ((h << 5) + h) + word[i];
    return h;
}

void kw_table_init(void) {
    int pool_idx = 0;
    for (int i = 0; c_keywords[i]; i++) {
        int len = (int)wcslen(c_keywords[i]);
        unsigned int h = kw_hash(c_keywords[i], len) & KW_HASH_MASK;
        int dup = 0;
        for (KwNode *n = g_kw_table[h]; n; n = n->next) {
            if (n->len == len && wcsncmp(n->word, c_keywords[i], len) == 0) { dup = 1; break; }
        }
        if (dup) continue;
        if (pool_idx >= 256) break;
        KwNode *n = &g_kw_pool[pool_idx++];
        n->word = c_keywords[i];
        n->len = len;
        n->next = g_kw_table[h];
        g_kw_table[h] = n;
    }
}

int is_c_keyword(const wchar_t *word, int len) {
    unsigned int h = kw_hash(word, len) & KW_HASH_MASK;
    for (KwNode *n = g_kw_table[h]; n; n = n->next) {
        if (n->len == len && wcsncmp(n->word, word, len) == 0) return 1;
    }
    return 0;
}

/* Single-pass line tokenizer for code — O(n) per line */
int tokenize_line_code(const wchar_t *chars, int line_len, SynToken *out, int in_block_comment) {
    int i = 0;

    for (i = 0; i < line_len; i++) out[i] = TOK_NORMAL;

    if (in_block_comment) {
        for (i = 0; i < line_len - 1; i++) {
            out[i] = TOK_COMMENT;
            if (chars[i] == L'*' && chars[i + 1] == L'/') {
                out[i + 1] = TOK_COMMENT;
                i += 2;
                goto normal_scan;
            }
        }
        if (i < line_len) out[i] = TOK_COMMENT;
        return 1;
    }

    {
        int j = 0;
        while (j < line_len && iswspace(chars[j])) j++;
        if (j < line_len && chars[j] == L'#') {
            if (j + 1 < line_len && iswalpha(chars[j + 1])) {
                for (int k = 0; k < line_len; k++) out[k] = TOK_PREPROCESSOR;
                return 0;
            }
        }
    }

    i = 0;
normal_scan:
    while (i < line_len) {
        wchar_t c = chars[i];

        if (c == L'/' && i + 1 < line_len && chars[i + 1] == L'*') {
            out[i] = TOK_COMMENT;
            out[i + 1] = TOK_COMMENT;
            i += 2;
            while (i < line_len) {
                if (i + 1 < line_len && chars[i] == L'*' && chars[i + 1] == L'/') {
                    out[i] = TOK_COMMENT;
                    out[i + 1] = TOK_COMMENT;
                    i += 2;
                    goto normal_scan;
                }
                out[i] = TOK_COMMENT;
                i++;
            }
            return 1;
        }

        if (c == L'/' && i + 1 < line_len && chars[i + 1] == L'/') {
            for (int j = i; j < line_len; j++) out[j] = TOK_COMMENT;
            return 0;
        }

        if (c == L'#' && (i == 0 || !iswalpha(chars[i - 1]))) {
            for (int j = i; j < line_len; j++) out[j] = TOK_COMMENT;
            return 0;
        }

        if (c == L'"' || c == L'\'') {
            wchar_t q = c;
            out[i] = TOK_STRING;
            i++;
            while (i < line_len) {
                wchar_t sc = chars[i];
                out[i] = TOK_STRING;
                if (sc == q) { i++; break; }
                if (sc == L'\\' && i + 1 < line_len) { i++; out[i] = TOK_STRING; }
                i++;
            }
            continue;
        }

        if (iswdigit(c) || (c == L'.' && i + 1 < line_len && iswdigit(chars[i + 1]))) {
            int ns = i;
            if (c == L'0' && i + 1 < line_len) {
                wchar_t next = chars[i + 1];
                if (next == L'x' || next == L'X') {
                    i += 2;
                    while (i < line_len && iswxdigit(chars[i])) i++;
                } else if (next == L'b' || next == L'B') {
                    i += 2;
                    while (i < line_len && (chars[i] == L'0' || chars[i] == L'1')) i++;
                } else {
                    goto decimal;
                }
            } else {
                decimal:
                while (i < line_len && (iswdigit(chars[i]) || chars[i] == L'.')) i++;
                if (i < line_len && (chars[i] == L'e' || chars[i] == L'E')) {
                    i++;
                    if (i < line_len && (chars[i] == L'+' || chars[i] == L'-')) i++;
                    while (i < line_len && iswdigit(chars[i])) i++;
                }
            }
            while (i < line_len && (chars[i] == L'u' || chars[i] == L'U' ||
                   chars[i] == L'l' || chars[i] == L'L' ||
                   chars[i] == L'f' || chars[i] == L'F')) i++;
            for (int j = ns; j < i; j++) out[j] = TOK_NUMBER;
            continue;
        }

        if (iswalpha(c) || c == L'_') {
            int ws = i;
            wchar_t word[64];
            int wl = 0;
            while (i < line_len && (iswalnum(chars[i]) || chars[i] == L'_')) {
                if (wl < 63) word[wl++] = chars[i];
                i++;
            }
            word[wl] = 0;

            SynToken tok = TOK_NORMAL;
            if (is_c_keyword(word, wl)) {
                tok = TOK_KEYWORD;
            } else if (i < line_len && chars[i] == L'(') {
                tok = TOK_FUNCTION;
            } else if (iswupper(word[0])) {
                tok = TOK_TYPE;
            }
            for (int j = ws; j < i; j++) out[j] = tok;
            continue;
        }

        if (wcschr(L"+-*/%=<>!&|^~?:", c)) {
            out[i] = TOK_OPERATOR;
            i++;
            continue;
        }

        i++;
    }
    return 0;
}

/* Single-pass line tokenizer for prose/markdown — O(n) per line */
void tokenize_line_prose(const wchar_t *chars, int line_len, SynToken *out) {
    for (int i = 0; i < line_len; i++) out[i] = TOK_NORMAL;
    if (line_len == 0) return;

    wchar_t first = chars[0];

    if (first == L'#') {
        for (int i = 0; i < line_len; i++) out[i] = TOK_MD_HEADING;
        return;
    }

    if (first == L'>') {
        for (int i = 0; i < line_len; i++) out[i] = TOK_MD_BLOCKQUOTE;
        return;
    }

    if ((first == L'-' || first == L'*' || first == L'+') &&
        line_len > 1 && chars[1] == L' ') {
        out[0] = TOK_MD_LIST;
    }

    if (iswdigit(first)) {
        int j = 0;
        while (j < line_len && iswdigit(chars[j])) j++;
        if (j < line_len && chars[j] == L'.' && j + 1 < line_len && chars[j + 1] == L' ') {
            for (int k = 0; k <= j; k++) out[k] = TOK_MD_LIST;
        }
    }

    if (first == L'-' || first == L'*' || first == L'_') {
        wchar_t rule_ch = first;
        int rule_count = 0;
        int is_rule = 1;
        for (int i = 0; i < line_len; i++) {
            if (chars[i] == rule_ch) rule_count++;
            else if (chars[i] != L' ') { is_rule = 0; break; }
        }
        if (is_rule && rule_count >= 3) {
            for (int i = 0; i < line_len; i++) out[i] = TOK_MD_HEADING;
        }
    }

    /* Inline code: `...` */
    {
        int i = 0;
        while (i < line_len) {
            if (chars[i] == L'`') {
                out[i] = TOK_MD_CODE;
                i++;
                while (i < line_len && chars[i] != L'`') {
                    out[i] = TOK_MD_CODE;
                    i++;
                }
                if (i < line_len) { out[i] = TOK_MD_CODE; i++; }
            } else {
                i++;
            }
        }
    }

    /* Bold: **text** */
    {
        int i = 0;
        while (i < line_len - 1) {
            if (out[i] != TOK_MD_CODE &&
                chars[i] == L'*' && chars[i + 1] == L'*') {
                out[i] = TOK_MD_BOLD;
                out[i + 1] = TOK_MD_BOLD;
                i += 2;
                while (i < line_len - 1) {
                    if (chars[i] == L'*' && chars[i + 1] == L'*') {
                        out[i] = TOK_MD_BOLD;
                        out[i + 1] = TOK_MD_BOLD;
                        i += 2;
                        break;
                    }
                    out[i] = TOK_MD_BOLD;
                    i++;
                }
            } else {
                i++;
            }
        }
    }

    /* Italic: *text* or _text_ */
    {
        int i = 0;
        while (i < line_len) {
            if (out[i] != TOK_MD_CODE && out[i] != TOK_MD_BOLD &&
                (chars[i] == L'*' || chars[i] == L'_')) {
                wchar_t delim = chars[i];
                if (i + 1 < line_len && chars[i + 1] != delim && chars[i + 1] != L' ') {
                    int start = i;
                    i++;
                    while (i < line_len && chars[i] != delim &&
                           out[i] != TOK_MD_CODE && out[i] != TOK_MD_BOLD) {
                        i++;
                    }
                    if (i < line_len && chars[i] == delim) {
                        for (int j = start; j <= i; j++) {
                            if (out[j] == TOK_NORMAL) out[j] = TOK_MD_ITALIC;
                        }
                        i++;
                    }
                } else {
                    i++;
                }
            } else {
                i++;
            }
        }
    }

    /* Links: [text](url) */
    {
        int i = 0;
        while (i < line_len) {
            if (out[i] == TOK_MD_CODE) { i++; continue; }
            if (chars[i] == L'[') {
                int bracket_start = i;
                i++;
                while (i < line_len && chars[i] != L']') i++;
                if (i < line_len && i + 1 < line_len && chars[i + 1] == L'(') {
                    int paren_start = i + 1;
                    i = paren_start + 1;
                    while (i < line_len && chars[i] != L')') i++;
                    if (i < line_len) {
                        for (int j = bracket_start; j <= i; j++) {
                            if (out[j] == TOK_NORMAL) out[j] = TOK_MD_LINK;
                        }
                        i++;
                    }
                } else {
                    i++;
                }
            } else {
                i++;
            }
        }
    }
}
