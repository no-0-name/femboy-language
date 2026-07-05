#ifndef _WIN32
#define _DEFAULT_SOURCE
#endif

#include "preprocessor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <stdlib.h>
#define FEMBOY_PATH_SEP '\\'
#define FEMBOY_PATH_BUF_SIZE 4096
#else
#include <stdlib.h>
#define FEMBOY_PATH_SEP '/'

#define FEMBOY_PATH_BUF_SIZE 4096
#endif

typedef struct VisitedFile {
    char *canonical_path;
    bool in_progress;

    struct VisitedFile *next;
} VisitedFile;

typedef struct {
    VisitedFile *visited;
} PreprocessState;

static char *canonicalize_path(const char *path) {
#ifdef _WIN32
    char *buf = femboy_malloc(FEMBOY_PATH_BUF_SIZE);
    if (!_fullpath(buf, path, FEMBOY_PATH_BUF_SIZE)) { free(buf); return femboy_strdup(path); }
    return buf;
#else

    char *resolved = realpath(path, NULL);
    if (!resolved) return femboy_strdup(path);
    return resolved;
#endif
}

static VisitedFile *find_visited(PreprocessState *ps, const char *canonical) {
    for (VisitedFile *v = ps->visited; v; v = v->next) {
        if (!strcmp(v->canonical_path, canonical)) return v;
    }
    return NULL;
}

static void free_visited(PreprocessState *ps) {
    VisitedFile *v = ps->visited;
    while (v) {
        VisitedFile *next = v->next;
        free(v->canonical_path);
        free(v);
        v = next;
    }
}

static FemboyStatus read_whole_file(const char *path, char **out, FemboyError *err) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return femboy_error_set(err, FEMBOY_ERR_IO, -1, -1, "failed to open file '%s'", path);
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return femboy_error_set(err, FEMBOY_ERR_IO, -1, -1, "failed to determine the size of file '%s'", path); }
    long size = ftell(f);
    if (size < 0) { fclose(f); return femboy_error_set(err, FEMBOY_ERR_IO, -1, -1, "failed to determine the size of file '%s'", path); }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return femboy_error_set(err, FEMBOY_ERR_IO, -1, -1, "failed to seek to the start of file '%s'", path); }

    char *buf = femboy_malloc((size_t)size + 1);
    size_t read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (read != (size_t)size) {
        free(buf);
        return femboy_error_set(err, FEMBOY_ERR_IO, -1, -1, "failed to read the entirety of file '%s'", path);
    }
    buf[size] = '\0';
    *out = buf;
    return FEMBOY_OK;
}

static void dirname_of(const char *path, char *dir, size_t dir_size) {
    const char *last_sep = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') last_sep = p;
    }
    if (!last_sep) {
        snprintf(dir, dir_size, ".");
        return;
    }
    size_t len = (size_t)(last_sep - path);
    if (len >= dir_size) len = dir_size - 1;
    memcpy(dir, path, len);
    dir[len] = '\0';
}

static char *dupstr_n_local(const char *s, size_t len) {
    char *r = femboy_malloc(len + 1);
    memcpy(r, s, len);
    r[len] = '\0';
    return r;
}

static bool try_parse_import_line(const char *line, int line_no, char **path_out, int *consumed_len,
                                   FemboyError *err, bool *had_error) {
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;

    if (strncmp(p, "import", 6) != 0) return false;
    const char *after_kw = p + 6;
    if (isalnum((unsigned char)*after_kw) || *after_kw == '_') return false;

    p = after_kw;
    while (*p == ' ' || *p == '\t') p++;

    if (*p != '"') {

        return false;
    }
    p++;
    const char *path_start = p;
    while (*p && *p != '"' && *p != '\n') p++;
    if (*p != '"') {
        femboy_error_set(err, FEMBOY_ERR_LEX, line_no, -1, "unterminated path string in an import directive");
        *had_error = true;
        return true;
    }
    size_t path_len = (size_t)(p - path_start);
    p++;

    while (*p == ' ' || *p == '\t') p++;
    if (*p != ';') {
        femboy_error_set(err, FEMBOY_ERR_LEX, line_no, -1, "expected ';' after an import directive");
        *had_error = true;
        return true;
    }
    p++;

    *path_out = dupstr_n_local(path_start, path_len);
    *consumed_len = (int)(p - line);
    return true;
}

typedef struct {
    char *buf;
    size_t len, cap;
} StrBuf;

static void sb_init(StrBuf *sb) {
    sb->cap = 4096;
    sb->buf = femboy_malloc(sb->cap);
    sb->len = 0;
}

static void sb_append(StrBuf *sb, const char *data, size_t n) {
    if (sb->len + n + 1 > sb->cap) {
        while (sb->len + n + 1 > sb->cap) sb->cap *= 2;
        sb->buf = femboy_realloc(sb->buf, sb->cap);
    }
    memcpy(sb->buf + sb->len, data, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
}

static FemboyStatus process_file_recursive(PreprocessState *ps, const char *path, StrBuf *out, FemboyError *err);

static FemboyStatus process_text(PreprocessState *ps, const char *path, const char *src, StrBuf *out, FemboyError *err) {
    char dir[2048];
    dirname_of(path, dir, sizeof dir);

    const char *line_start = src;
    int line_no = 1;

    while (*line_start) {
        const char *line_end = strchr(line_start, '\n');
        size_t raw_line_len = line_end ? (size_t)(line_end - line_start) : strlen(line_start);

        char *import_path = NULL;
        int consumed_len = 0;
        bool had_error = false;
        bool is_import = try_parse_import_line(line_start, line_no, &import_path, &consumed_len, err, &had_error);

        if (is_import && had_error) {
            return FEMBOY_ERR_LEX;
        }

        if (is_import) {

            char full_path[4096];
            snprintf(full_path, sizeof full_path, "%s%c%s", dir, FEMBOY_PATH_SEP, import_path);
            free(import_path);

            FemboyStatus st = process_file_recursive(ps, full_path, out, err);
            if (st != FEMBOY_OK) return st;

            const char *rest = line_start + consumed_len;
            size_t rest_len = raw_line_len - (size_t)consumed_len;
            sb_append(out, rest, rest_len);
            sb_append(out, "\n", 1);
        } else {
            sb_append(out, line_start, raw_line_len);
            sb_append(out, "\n", 1);
        }

        line_no++;
        if (!line_end) break;
        line_start = line_end + 1;
    }

    return FEMBOY_OK;
}

static FemboyStatus process_file_recursive(PreprocessState *ps, const char *path, StrBuf *out, FemboyError *err) {
    char *canonical = canonicalize_path(path);

    VisitedFile *existing = find_visited(ps, canonical);
    if (existing) {
        if (existing->in_progress) {
            femboy_error_set(err, FEMBOY_ERR_IO, -1, -1,
                             "cyclic import: file '%s' imports itself (directly or through a chain of other imports)",
                             path);
            free(canonical);
            return FEMBOY_ERR_IO;
        }

        free(canonical);
        return FEMBOY_OK;
    }

    VisitedFile *v = femboy_malloc(sizeof(VisitedFile));
    v->canonical_path = canonical;
    v->in_progress = true;
    v->next = ps->visited;
    ps->visited = v;

    char *src = NULL;
    FemboyStatus st = read_whole_file(path, &src, err);
    if (st != FEMBOY_OK) {

        return st;
    }

    st = process_text(ps, path, src, out, err);
    free(src);
    if (st != FEMBOY_OK) return st;

    v->in_progress = false;
    return FEMBOY_OK;
}

FemboyStatus femboy_preprocess_file(const char *entry_path, char **out, FemboyError *err) {
    PreprocessState ps = { .visited = NULL };
    StrBuf sb;
    sb_init(&sb);

    FemboyStatus st = process_file_recursive(&ps, entry_path, &sb, err);
    free_visited(&ps);

    if (st != FEMBOY_OK) {
        free(sb.buf);
        return st;
    }

    *out = sb.buf;
    return FEMBOY_OK;
}
