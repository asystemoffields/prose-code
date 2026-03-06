#include "prose_code.h"

void load_file(Document *doc, const wchar_t *path) {
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    LARGE_INTEGER li_size;
    if (!GetFileSizeEx(hFile, &li_size) || li_size.QuadPart > (LONGLONG)(512 * 1024 * 1024)) {
        CloseHandle(hFile);
        return;
    }
    int size = (int)li_size.QuadPart;

    char *raw = (char *)malloc(size + 1);
    if (!raw) { CloseHandle(hFile); return; }
    DWORD bytes_read = 0;
    if (!ReadFile(hFile, raw, (DWORD)size, &bytes_read, NULL)) {
        free(raw); CloseHandle(hFile); return;
    }
    raw[bytes_read] = 0;
    CloseHandle(hFile);
    size = (int)bytes_read;

    char *start = raw;
    if (size >= 3 && (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) {
        start += 3;
        size -= 3;
    }

    int wlen = MultiByteToWideChar(CP_UTF8, 0, start, (int)size, NULL, 0);
    wchar_t *wtext = (wchar_t *)malloc((wlen + 1) * sizeof(wchar_t));
    if (!wtext) { free(raw); return; }
    MultiByteToWideChar(CP_UTF8, 0, start, (int)size, wtext, wlen);
    wtext[wlen] = 0;
    free(raw);

    int j = 0;
    for (int i = 0; i < wlen; i++) {
        if (wtext[i] != L'\r') wtext[j++] = wtext[i];
    }
    wtext[j] = 0;

    gb_free(&doc->gb);
    gb_init(&doc->gb, j + GAP_INIT);
    gb_insert(&doc->gb, 0, wtext, j);
    free(wtext);

    safe_wcscpy(doc->filepath, MAX_PATH, path);
    const wchar_t *slash = wcsrchr(path, L'\\');
    if (!slash) slash = wcsrchr(path, L'/');
    safe_wcscpy(doc->title, 64, slash ? slash + 1 : path);

    const wchar_t *ext = wcsrchr(path, L'.');
    if (ext) {
        if (_wcsicmp(ext, L".c") == 0 || _wcsicmp(ext, L".h") == 0 ||
            _wcsicmp(ext, L".cpp") == 0 || _wcsicmp(ext, L".hpp") == 0 ||
            _wcsicmp(ext, L".py") == 0 || _wcsicmp(ext, L".js") == 0 ||
            _wcsicmp(ext, L".ts") == 0 || _wcsicmp(ext, L".rs") == 0 ||
            _wcsicmp(ext, L".java") == 0 || _wcsicmp(ext, L".cs") == 0 ||
            _wcsicmp(ext, L".go") == 0 || _wcsicmp(ext, L".rb") == 0 ||
            _wcsicmp(ext, L".sh") == 0 || _wcsicmp(ext, L".json") == 0 ||
            _wcsicmp(ext, L".xml") == 0 || _wcsicmp(ext, L".html") == 0 ||
            _wcsicmp(ext, L".css") == 0 || _wcsicmp(ext, L".sql") == 0 ||
            _wcsicmp(ext, L".yaml") == 0 || _wcsicmp(ext, L".toml") == 0) {
            doc->mode = MODE_CODE;
        } else {
            doc->mode = MODE_PROSE;
        }
    }

    doc->cursor = 0;
    doc->sel_anchor = -1;
    doc->scroll_y = 0;
    doc->target_scroll_y = 0;
    doc->scroll_x = 0;
    doc->target_scroll_x = 0;
    doc->modified = 0;
    doc->bc_cached_mutation = -1;
    doc->bc_cached_line = -1;
    undo_clear(&doc->undo);
    recalc_lines(doc);
    update_stats(doc);
    snapshot_session_baseline(doc);
}

char *doc_to_utf8(Document *doc, int *out_len) {
    GapBuffer *gb = &doc->gb;
    bpos len = gb_length(gb);

    if (len == 0) {
        char *empty = (char *)malloc(1);
        if (empty) empty[0] = 0;
        *out_len = 0;
        return empty;
    }

    wchar_t *text = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (!text) { *out_len = 0; return NULL; }
    gb_copy_range(gb, 0, len, text);
    text[len] = 0;

    bpos newlines = 0;
    for (bpos i = 0; i < len; i++) {
        if (text[i] == L'\n') newlines++;
    }

    wchar_t *winfmt = (wchar_t *)malloc((len + newlines + 1) * sizeof(wchar_t));
    if (!winfmt) { free(text); *out_len = 0; return NULL; }
    bpos j = 0;
    for (bpos i = 0; i < len; i++) {
        if (text[i] == L'\n') winfmt[j++] = L'\r';
        winfmt[j++] = text[i];
    }
    winfmt[j] = 0;
    free(text);

    int utf8len = WideCharToMultiByte(CP_UTF8, 0, winfmt, (int)j, NULL, 0, NULL, NULL);
    char *utf8 = (char *)malloc(utf8len);
    if (!utf8) { free(winfmt); *out_len = 0; return NULL; }
    WideCharToMultiByte(CP_UTF8, 0, winfmt, (int)j, utf8, utf8len, NULL, NULL);
    free(winfmt);

    *out_len = utf8len;
    return utf8;
}

int write_file_atomic(const wchar_t *final_path, const char *data, int data_len) {
    wchar_t tmp_path[MAX_PATH + 16];
    swprintf(tmp_path, MAX_PATH + 16, L"%ls.tmp~", final_path);

    HANDLE hFile = CreateFileW(tmp_path, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return 0;

    DWORD written;
    BOOL ok = WriteFile(hFile, data, (DWORD)data_len, &written, NULL);
    if (!ok || (int)written != data_len) {
        CloseHandle(hFile);
        DeleteFileW(tmp_path);
        return 0;
    }

    FlushFileBuffers(hFile);
    CloseHandle(hFile);

    if (GetFileAttributesW(final_path) != INVALID_FILE_ATTRIBUTES) {
        wchar_t bak_path[MAX_PATH + 16];
        swprintf(bak_path, MAX_PATH + 16, L"%ls.bak~", final_path);
        if (ReplaceFileW(final_path, tmp_path, bak_path,
                         REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL)) {
            return 1;
        }
    }
    if (MoveFileExW(tmp_path, final_path,
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return 1;
    }

    DeleteFileW(final_path);
    if (MoveFileW(tmp_path, final_path)) return 1;

    DeleteFileW(tmp_path);
    return 0;
}

void save_file(Document *doc, const wchar_t *path) {
    int utf8len;
    char *utf8 = doc_to_utf8(doc, &utf8len);
    if (!utf8) return;

    if (write_file_atomic(path, utf8, utf8len)) {
        doc->modified = 0;
        doc->undo.save_point = doc->undo.current;
        safe_wcscpy(doc->filepath, MAX_PATH, path);
        const wchar_t *slash = wcsrchr(path, L'\\');
        if (!slash) slash = wcsrchr(path, L'/');
        safe_wcscpy(doc->title, 64, slash ? slash + 1 : path);

        autosave_delete_for_doc(doc);
        doc->autosave_mutation_snapshot = doc->gb.mutation;
    }
    free(utf8);
}

/* ── Autosave shadow file system ── */

void autosave_ensure_dir(void) {
    if (g_editor.autosave_dir[0]) return;

    wchar_t appdata[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", appdata, MAX_PATH) == 0)
        return;

    swprintf(g_editor.autosave_dir, MAX_PATH, L"%ls\\ProseCode\\autosave", appdata);

    wchar_t parent[MAX_PATH];
    swprintf(parent, MAX_PATH, L"%ls\\ProseCode", appdata);
    CreateDirectoryW(parent, NULL);
    CreateDirectoryW(g_editor.autosave_dir, NULL);
}

static unsigned int path_hash(const wchar_t *path) {
    unsigned int h = 5381;
    while (*path) {
        h = ((h << 5) + h) ^ (unsigned int)(*path);
        path++;
    }
    return h;
}

void autosave_path_for_doc(Document *doc, wchar_t *out) {
    autosave_ensure_dir();
    if (doc->filepath[0]) {
        unsigned int h = path_hash(doc->filepath);
        swprintf(out, MAX_PATH + 32, L"%ls\\%08x.pctmp", g_editor.autosave_dir, h);
    } else {
        swprintf(out, MAX_PATH + 32, L"%ls\\untitled_%u_%u.pctmp",
                 g_editor.autosave_dir, doc->autosave_id, g_editor.session_start_time);
    }
}

static int json_escape_path(const char *src, char *dst, int dst_size) {
    int j = 0;
    for (int i = 0; src[i] && j < dst_size - 2; i++) {
        char c = src[i];
        if (c == '"' || c == '\\') {
            dst[j++] = '\\';
            if (j >= dst_size - 1) break;
        }
        dst[j++] = c;
    }
    dst[j] = 0;
    return j;
}

void autosave_write(Document *doc) {
    int utf8len;
    char *utf8 = doc_to_utf8(doc, &utf8len);
    if (!utf8) return;

    char header[2048];
    char narrow_path[MAX_PATH * 3];
    char escaped_path[MAX_PATH * 6];
    WideCharToMultiByte(CP_UTF8, 0, doc->filepath, -1, narrow_path,
                        sizeof(narrow_path), NULL, NULL);
    json_escape_path(narrow_path, escaped_path, sizeof(escaped_path));
    int hdr_len = snprintf(header, sizeof(header),
        "{\"path\":\"%s\",\"time\":%u,\"chars\":%lld}\n",
        escaped_path, (unsigned)GetTickCount(), (long long)gb_length(&doc->gb));
    if (hdr_len < 0 || hdr_len >= (int)sizeof(header))
        hdr_len = (int)sizeof(header) - 1;

    int total = 4 + hdr_len + utf8len;
    char *buf = (char *)malloc(total);
    if (!buf) { free(utf8); return; }

    buf[0] = (char)(hdr_len & 0xFF);
    buf[1] = (char)((hdr_len >> 8) & 0xFF);
    buf[2] = (char)((hdr_len >> 16) & 0xFF);
    buf[3] = (char)((hdr_len >> 24) & 0xFF);
    memcpy(buf + 4, header, hdr_len);
    memcpy(buf + 4 + hdr_len, utf8, utf8len);
    free(utf8);

    wchar_t shadow_path[MAX_PATH + 32];
    autosave_path_for_doc(doc, shadow_path);
    write_file_atomic(shadow_path, buf, total);
    free(buf);

    doc->autosave_mutation_snapshot = doc->gb.mutation;
    doc->autosave_last_time = GetTickCount();
}

void autosave_tick(void) {
    for (int i = 0; i < g_editor.tab_count; i++) {
        Document *doc = g_editor.tabs[i];
        if (!doc->modified) continue;
        if (doc->gb.mutation == doc->autosave_mutation_snapshot) continue;
        autosave_write(doc);
    }
}

void autosave_delete_for_doc(Document *doc) {
    wchar_t shadow_path[MAX_PATH + 32];
    autosave_path_for_doc(doc, shadow_path);
    DeleteFileW(shadow_path);
}

void autosave_cleanup_all(void) {
    autosave_ensure_dir();
    if (!g_editor.autosave_dir[0]) return;

    wchar_t pattern[MAX_PATH];
    swprintf(pattern, MAX_PATH, L"%ls\\*.pctmp", g_editor.autosave_dir);

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        wchar_t full[MAX_PATH];
        swprintf(full, MAX_PATH, L"%ls\\%ls", g_editor.autosave_dir, fd.cFileName);
        DeleteFileW(full);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

void autosave_cleanup_tmp(void) {
    autosave_ensure_dir();
    if (!g_editor.autosave_dir[0]) return;

    wchar_t pattern[MAX_PATH];
    swprintf(pattern, MAX_PATH, L"%ls\\*.tmp~", g_editor.autosave_dir);

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        wchar_t full[MAX_PATH];
        swprintf(full, MAX_PATH, L"%ls\\%ls", g_editor.autosave_dir, fd.cFileName);
        DeleteFileW(full);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

void autosave_recover(void) {
    autosave_ensure_dir();
    if (!g_editor.autosave_dir[0]) return;

    wchar_t pattern[MAX_PATH];
    swprintf(pattern, MAX_PATH, L"%ls\\*.pctmp", g_editor.autosave_dir);

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    int recovered = 0;
    do {
        wchar_t full[MAX_PATH];
        swprintf(full, MAX_PATH, L"%ls\\%ls", g_editor.autosave_dir, fd.cFileName);

        HANDLE hFile = CreateFileW(full, GENERIC_READ, FILE_SHARE_READ, NULL,
                                   OPEN_EXISTING, 0, NULL);
        if (hFile == INVALID_HANDLE_VALUE) continue;

        DWORD file_size = GetFileSize(hFile, NULL);
        if (file_size < 8) { CloseHandle(hFile); DeleteFileW(full); continue; }

        char *raw = (char *)malloc(file_size);
        if (!raw) { CloseHandle(hFile); continue; }
        DWORD bytes_read;
        ReadFile(hFile, raw, file_size, &bytes_read, NULL);
        CloseHandle(hFile);

        if (bytes_read < 8) { free(raw); DeleteFileW(full); continue; }

        int hdr_len = (unsigned char)raw[0] |
                      ((unsigned char)raw[1] << 8) |
                      ((unsigned char)raw[2] << 16) |
                      ((unsigned char)raw[3] << 24);

        if (hdr_len <= 0 || hdr_len >= 1024 || 4 + hdr_len > (int)bytes_read) {
            free(raw); DeleteFileW(full); continue;
        }

        char hdr_buf[1024];
        memcpy(hdr_buf, raw + 4, hdr_len);
        hdr_buf[hdr_len] = 0;

        wchar_t orig_path[MAX_PATH] = {0};
        char *pp = strstr(hdr_buf, "\"path\":\"");
        if (pp) {
            pp += 8;
            char *pe = strchr(pp, '"');
            if (pe) {
                *pe = 0;
                MultiByteToWideChar(CP_UTF8, 0, pp, -1, orig_path, MAX_PATH);
            }
        }

        wchar_t msg[512];
        const wchar_t *name = orig_path[0] ? wcsrchr(orig_path, L'\\') : NULL;
        if (name) name++;
        else if (orig_path[0]) name = orig_path;
        else name = L"Untitled";
        swprintf(msg, 512,
                 L"Prose_Code found unsaved work:\n\n  \"%ls\"\n\nRecover this file?",
                 name);

        if (MessageBoxW(g_editor.hwnd, msg, L"Crash Recovery",
                        MB_YESNO | MB_ICONINFORMATION) == IDYES) {
            char *content = raw + 4 + hdr_len;
            int content_len = (int)bytes_read - 4 - hdr_len;

            int wlen = MultiByteToWideChar(CP_UTF8, 0, content, content_len, NULL, 0);
            wchar_t *wtext = (wchar_t *)malloc((wlen + 1) * sizeof(wchar_t));
            if (!wtext) { free(raw); DeleteFileW(full); continue; }
            MultiByteToWideChar(CP_UTF8, 0, content, content_len, wtext, wlen);
            wtext[wlen] = 0;

            int j = 0;
            for (int i = 0; i < wlen; i++) {
                if (wtext[i] != L'\r') wtext[j++] = wtext[i];
            }

            Document *doc = doc_create();
            gb_insert(&doc->gb, 0, wtext, j);
            free(wtext);

            if (orig_path[0]) {
                safe_wcscpy(doc->filepath, MAX_PATH, orig_path);
                const wchar_t *slash = wcsrchr(orig_path, L'\\');
                if (!slash) slash = wcsrchr(orig_path, L'/');
                safe_wcscpy(doc->title, 64, slash ? slash + 1 : orig_path);
            } else {
                safe_wcscpy(doc->title, 64, L"Recovered");
            }
            doc->modified = 1;
            recalc_lines(doc);
            update_stats(doc);
            snapshot_session_baseline(doc);

            if (g_editor.tab_count < MAX_TABS) {
                if (g_editor.tab_count == 1 && !g_editor.tabs[0]->modified &&
                    !g_editor.tabs[0]->filepath[0] &&
                    gb_length(&g_editor.tabs[0]->gb) == 0) {
                    doc_free(g_editor.tabs[0]);
                    g_editor.tabs[0] = doc;
                    g_editor.active_tab = 0;
                } else {
                    g_editor.tabs[g_editor.tab_count] = doc;
                    g_editor.active_tab = g_editor.tab_count;
                    g_editor.tab_count++;
                }
                recovered++;
            } else {
                doc_free(doc);
            }
        }

        free(raw);
        DeleteFileW(full);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    if (recovered > 0) {
        InvalidateRect(g_editor.hwnd, NULL, FALSE);
    }
}

int prompt_save_doc(int tab_idx) {
    if (tab_idx < 0 || tab_idx >= g_editor.tab_count) return 1;
    Document *doc = g_editor.tabs[tab_idx];
    if (!doc->modified) return 1;

    wchar_t msg[256];
    swprintf(msg, 256, L"Save changes to \"%ls\"?", doc->title);
    int result = MessageBoxW(g_editor.hwnd, msg, L"Prose_Code",
                             MB_YESNOCANCEL | MB_ICONWARNING);
    if (result == IDYES) {
        return save_file_dialog_for_doc(doc);
    } else if (result == IDNO) {
        autosave_delete_for_doc(doc);
        return 1;
    }
    return 0;
}

void open_file_dialog(void) {
    wchar_t path[MAX_PATH] = {0};
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_editor.hwnd;
    ofn.lpstrFilter = L"All Files\0*.*\0Text Files\0*.txt;*.md;*.markdown\0C/C++ Files\0*.c;*.h;*.cpp;*.hpp\0Python Files\0*.py\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        Document *doc = current_doc();
        int reuse = 0;
        if (doc && !doc->modified && doc->filepath[0] == 0 && gb_length(&doc->gb) == 0) {
            reuse = 1;
        }

        if (reuse) {
            load_file(doc, path);
        } else {
            if (g_editor.tab_count < MAX_TABS) {
                doc = doc_create();
                load_file(doc, path);
                g_editor.tabs[g_editor.tab_count] = doc;
                g_editor.active_tab = g_editor.tab_count;
                g_editor.tab_count++;
            }
        }
        InvalidateRect(g_editor.hwnd, NULL, FALSE);
    }
}

void save_file_dialog(void) {
    Document *doc = current_doc();
    if (!doc) return;

    if (doc->filepath[0]) {
        save_file(doc, doc->filepath);
        return;
    }

    wchar_t path[MAX_PATH] = {0};
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_editor.hwnd;
    ofn.lpstrFilter = L"All Files\0*.*\0Text Files\0*.txt\0Markdown\0*.md\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;

    if (GetSaveFileNameW(&ofn)) {
        save_file(doc, path);
    }
}

int save_file_dialog_for_doc(Document *doc) {
    if (!doc) return 0;

    if (doc->filepath[0]) {
        save_file(doc, doc->filepath);
        return doc->modified ? 0 : 1;
    }

    wchar_t path[MAX_PATH] = {0};
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_editor.hwnd;
    ofn.lpstrFilter = L"All Files\0*.*\0Text Files\0*.txt\0Markdown\0*.md\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;

    if (!GetSaveFileNameW(&ofn)) return 0;
    save_file(doc, path);
    return doc->modified ? 0 : 1;
}

void save_current_file(void) {
    Document *doc = current_doc();
    if (!doc) return;
    if (doc->filepath[0]) {
        save_file(doc, doc->filepath);
    } else {
        save_file_dialog();
    }
}

void new_tab(void) {
    if (g_editor.tab_count >= MAX_TABS) return;
    Document *doc = doc_create();
    g_editor.tabs[g_editor.tab_count] = doc;
    g_editor.active_tab = g_editor.tab_count;
    g_editor.tab_count++;
    recalc_lines(doc);
    snapshot_session_baseline(doc);
}

void close_tab(int idx) {
    if (idx < 0 || idx >= g_editor.tab_count) return;
    if (!prompt_save_doc(idx)) return;
    autosave_delete_for_doc(g_editor.tabs[idx]);
    doc_free(g_editor.tabs[idx]);
    for (int i = idx; i < g_editor.tab_count - 1; i++) {
        g_editor.tabs[i] = g_editor.tabs[i + 1];
    }
    g_editor.tab_count--;
    if (g_editor.active_tab >= g_editor.tab_count)
        g_editor.active_tab = g_editor.tab_count - 1;
    if (g_editor.tab_count == 0) new_tab();
}
