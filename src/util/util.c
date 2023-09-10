#include "util.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>  // malloc
#include <string.h>  // strcmp
#include <sys/stat.h>

#include "../version.h"
#include "table.h"

int isalnum_(int c) {
  return isalnum(c) || c == '_';
}

int isutf8first(int c_) {
  unsigned int c = c_;
  if (c < 0xc0)
    return 0;  // FALSE
  if (c >= 0xfc)
    return 6;
  if (c >= 0xf8)
    return 5;
  if (c >= 0xf0)
    return 4;
  if (c >= 0xe0)
    return 3;
  return 2;
}

int isutf8follow(int c) {
  return c >= 0x80 && c < 0xc0;
}

int isoctal(int c) {
  return '0' <= c && c <= '7';
}

int xvalue(char c) {
  return ('0' <= c && c <= '9') ? c - '0' :
         ('A' <= c && c <= 'F') ? c - ('A' - 10) :
         ('a' <= c && c <= 'f') ? c - ('a' - 10) :
         -1;
}

bool starts_with(const char *str, const char *prefix) {
  size_t len = strlen(prefix);
  return strncmp(str, prefix, len) == 0;
}

int most_significant_bit(size_t x) {
  int bit;
  for (bit = 0;; ++bit) {
    x >>= 1;
    if (x <= 0)
      return bit;
  }
}

void *malloc_or_die(size_t size) {
  void *p = malloc(size);
  if (p == NULL) {
    fprintf(stderr, "memory overflow\n");
    exit(1);
  }
  return p;
}

void *realloc_or_die(void *ptr, size_t size) {
  void *p = realloc(ptr, size);
  if (p == NULL) {
    fprintf(stderr, "memory overflow\n");
    exit(1);
  }
  return p;
}

const Name *alloc_label(void) {
  static int label_no;
  ++label_no;
  char buf[2 + sizeof(int) * 3 + 1];
  snprintf(buf, sizeof(buf), ".L%04d", label_no);
  return alloc_name(buf, NULL, true);
}

ssize_t getline_chomp(char **lineptr, size_t *n, FILE *stream) {
  ssize_t len = getline(lineptr, n, stream);
  if (len > 0) {
    char *line = *lineptr;
    if (line[len - 1] == '\n')
      line[--len] = '\0';
  }
  return len;
}

static ssize_t getline_cat(char **lineptr, size_t *n, FILE *stream, size_t curlen) {
  char *nextline = NULL;
  size_t capa = 0;
  ssize_t len = getline_chomp(&nextline, &capa, stream);
  if (len == -1)
    return -1;
  if (len > 0) {
    char *oldline = *lineptr;
    char *reallocated = realloc(oldline, curlen + len + 1);
    if (reallocated == NULL)
      return -1;

    memcpy(reallocated + curlen, nextline, len + 1);
    *lineptr = reallocated;
    *n = curlen + len;  // '\0' is not included.
    free(nextline);
  }
  return curlen + len;
}

ssize_t getline_cont(char **lineptr, size_t *capa, FILE *stream, int *plineno) {
  int lineno = *plineno;
  ssize_t len = getline_chomp(lineptr, capa, stream);
  if (len != -1) {
    // Continue line.
    while (++lineno, len > 0 && (*lineptr)[len - 1] == '\\') {
      (*lineptr)[--len] = '\0';
      ssize_t nextlen = getline_cat(lineptr, capa, stream, len);
      if (nextlen == -1)
        break;
      len = nextlen;
    }
  }
  *plineno = lineno;
  return len;
}

bool is_fullpath(const char *filename) {
  if (*filename != '/')
    return false;
  for (const char *p = filename;;) {
    p = strstr(p, "/..");
    if (p == NULL)
      return true;
    if (p[3] == '/' || p[3] == '\0')
      return false;
    p += 3;
  }
}

char *join_paths(const char *paths[]) {
  StringBuffer sb;
  sb_init(&sb);
  int parent_count = 0;
  enum Top {
    OTHER,
    ROOTDIR,  // /
    CURDIR,   // .
  };
  enum Top top = OTHER;

  const char *p;
  for (const char **pp = paths; (p = *pp++) != NULL; ) {
    if (*p == '/') {  // Root.
      sb_init(&sb);
      parent_count = 0;
      top = ROOTDIR;
    }

    for (bool end = false; !end;) {
      while (*p == '/')
        ++p;
      if (*p == '\0')
        break;

      const char *q = strchr(p, '/');
      if (q == NULL) {  // Last.
        q = p + strlen(p);
        end = true;
      }
      ptrdiff_t len = q - p;
      if (len == 1 && *p == '.') {
        if (sb.elems->len == 0 && top == OTHER)
          top = CURDIR;
      } else if (len == 2 && strncmp(p, "..", 2) == 0) {
        if (sb.elems->len > 0) {
          void *elem = vec_pop(sb.elems);
          free(elem);
        } else {
          if (top == ROOTDIR) {
            // Illegal.
            return NULL;
          }
          ++parent_count;
          top = OTHER;
        }
      } else {
        sb_append(&sb, p, q);
      }
      p = q;
    }
  }

  for (; parent_count > 0; --parent_count)
    sb_prepend(&sb, "..", NULL);
  switch (top) {
  case CURDIR:   sb_prepend(&sb, ".", NULL); break;
  case ROOTDIR:  sb_prepend(&sb, sb.elems->len > 0 ? "" : "/", NULL); break;
  case OTHER: break;
  }
  return sb_join(&sb, "/");
}

char *get_ext(const char *filename) {
  const char *last_slash = strrchr(filename, '/');
  if (last_slash == NULL)
    last_slash = filename;
  char *dot = strrchr(last_slash, '.');
  return dot != NULL ? (char*)&dot[1]: (char*)&last_slash[strlen(last_slash)];
}

char *change_ext(const char *path, const char *ext) {
  const char *p = strrchr(path, '/');
  if (p == NULL)
    p = path;

  const char *q = strrchr(p, '.');
  size_t len = q != NULL ? (size_t)(q - path) : strlen(path);
  size_t ext_len = strlen(ext);
  char *s = malloc(len + 1 + ext_len);
  if (s != NULL) {
    memcpy(s, path, len);
    s[len] = '.';
    strcpy(s + (len + 1), ext);
  }
  return s;
}

void put_padding(FILE *fp, uintptr_t start) {
  long cur = ftell(fp);
  if (start > (size_t)cur) {
    size_t size = start - (uintptr_t)cur;
    for (size_t i = 0; i < size; ++i)
      fputc(0x00, fp);
  }
}

bool is_file(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);  // Include symbolic link, too.
}

void show_version(const char *exe) {
  printf("%s %s\n", exe, VERSION);
}

void error(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "\n");
  exit(1);
}

void show_error_line(const char *line, const char *p, int len) {
  fprintf(stderr, "%s\n", line);
  size_t pos = p - line;
  if (pos <= strlen(line)) {
    for (size_t i = 0; i < pos; ++i)
      fputc(line[i] == '\t' ? '\t' : ' ', stderr);
    fprintf(stderr, "^");
    for (int i = 1; i < len; ++i)
      fprintf(stderr, "~");
    fprintf(stderr, "\n");
  }
}

bool is_im8(intptr_t x) {
  return x <= ((1L << 7) - 1) && x >= -(1L << 7);
}

bool is_im16(intptr_t x) {
  return x <= ((1L << 15) - 1) && x >= -(1L << 15);
}

bool is_im32(intptr_t x) {
  return x <= ((1L << 31) - 1) && x >= -(1L << 31);
}

const char *skip_whitespaces(const char *s) {
  while (isspace(*s))
    ++s;
  return s;
}

int64_t wrap_value(int64_t value, int size, bool is_unsigned) {
  if (is_unsigned) {
    switch (size) {
    case 1:  value = (uint8_t)value; break;
    case 2:  value = (uint16_t)value; break;
    case 4:  value = (uint32_t)value; break;
    default:  break;
    }
  } else {
    switch (size) {
    case 1:  value = (int8_t)value; break;
    case 2:  value = (int16_t)value; break;
    case 4:  value = (int32_t)value; break;
    default:  break;
    }
  }
  return value;
}

// Container

#define BUF_MIN    (16 / 2)
#define BUF_ALIGN  (16)

void buf_put(Buffer *buf, const void *data, size_t bytes) {
  size_t size = buf->size;
  size_t newsize = size + bytes;

  if (newsize > buf->capa) {
    size_t newcapa = ALIGN(MAX(newsize, BUF_MIN) * 2, BUF_ALIGN);
    unsigned char *p = realloc(buf->data, newcapa);
    if (p == NULL)
      error("not enough memory");
    buf->data = p;
    buf->capa = newcapa;
  }

  memcpy(buf->data + size, data, bytes);
  buf->size = newsize;
}

void buf_align(Buffer *buf, int align) {
  size_t size = buf->size;
  size_t aligned_size = ALIGN(size, align);
  size_t add = aligned_size - size;
  if (add <= 0)
    return;

  void *zero = calloc(add, 1);
  buf_put(buf, zero, add);
  free(zero);

  assert(buf->size == aligned_size);
}

Vector *new_vector(void) {
  Vector *vec = malloc_or_die(sizeof(Vector));
  vec->data = NULL;
  vec->capacity = 0;
  vec->len = 0;
  return vec;
}

void free_vector(Vector *vec) {
  free(vec->data);
  free(vec);
}

void vec_clear(Vector *vec) {
  vec->len = 0;
}

void vec_push(Vector *vec, const void *elem) {
  if (vec->capacity <= vec->len) {
    if (vec->capacity <= 0)
      vec->capacity = 16;
    else
      vec->capacity <<= 1;
    vec->data = realloc_or_die(vec->data, sizeof(*vec->data) * vec->capacity);
  }
  vec->data[vec->len++] = (void*)elem;
}

void *vec_pop(Vector *vec) {
  return vec->len > 0 ? vec->data[--vec->len] : NULL;
}

void vec_insert(Vector *vec, int pos, const void *elem) {
  int len = vec->len;
  if (pos < 0 || pos > len)
    return;

  if (pos < len) {
    vec_push(vec, NULL);
    memmove(&vec->data[pos + 1], &vec->data[pos], sizeof(void*) * (len - pos));
    vec->data[pos] = (void*)elem;
  } else {
    vec_push(vec, elem);
  }
}

void vec_remove_at(Vector *vec, int index) {
  if (index < 0 || index >= vec->len)
    return;
  int d = vec->len - index - 1;
  if (d > 0)
    memmove(&vec->data[index], &vec->data[index + 1], d * sizeof(*vec->data));
  --vec->len;
}

bool vec_contains(Vector *vec, void *elem) {
  for (int i = 0, len = vec->len; i < len; ++i) {
    if (vec->data[i] == elem)
      return true;
  }
  return false;
}

// StringBuffer

typedef struct {
  const char *start;
  size_t len;
} StringElement;

void sb_init(StringBuffer *sb) {
  sb->elems = new_vector();
}

void sb_clear(StringBuffer *sb) {
  vec_clear(sb->elems);
}

bool sb_empty(StringBuffer *sb) {
  return sb->elems->len == 0;
}

void sb_insert(StringBuffer *sb, int pos, const char *start, const char *end) {
  StringElement *elem = malloc(sizeof(*elem));
  if (elem != NULL) {
    elem->start = start;
    elem->len = end != NULL ? (size_t)(end - start) : strlen(start);
    assert(0 <= pos && pos <= sb->elems->len);
    vec_insert(sb->elems, pos, elem);
  }
}

char *sb_join(StringBuffer *sb, const char *separator) {
  size_t total_len = 0;
  int count = sb->elems->len;
  for (int i = 0; i < count; ++i) {
    StringElement *elem = sb->elems->data[i];
    total_len += elem->len;
  }
  size_t sepalen = separator != NULL ? strlen(separator) : 0;
  if (count > 0 && sepalen > 0)
    total_len += sepalen * (count - 1);

  char *str = malloc(total_len + 1);
  if (str != NULL) {
    char *p = str;
    for (int i = 0; i < count; ++i) {
      if (i > 0 && sepalen > 0) {
        memcpy(p, separator, sepalen);
        p += sepalen;
      }
      StringElement *elem = sb->elems->data[i];
      memcpy(p, elem->start, elem->len);
      p += elem->len;
    }
    *p = '\0';
  }
  return str;
}

static const char *escape(int c) {
  switch (c) {
  case '\0': return "\\0";
  case '\n': return "\\n";
  case '\r': return "\\r";
  case '\t': return "\\t";
  case '"': return "\\\"";
  case '\\': return "\\\\";
  default:
    if (c < 0x20 || c >= 0x7f) {
      char *s = malloc(5);
      if (s != NULL) {
        snprintf(s, 5, "\\x%02x", c & 0xff);
        return s;
      }
    }
    return NULL;
  }
}

void escape_string(const char *str, size_t size, StringBuffer *sb) {
  const char *s, *p;
  const char *end = str + size;
  for (s = p = str; p < end; ++p) {
    const char *e = escape(*p);
    if (e == NULL)
      continue;

    if (p > s)
      sb_append(sb, s, p);
    sb_append(sb, e, NULL);
    s = p + 1;
  }
  if (p > s)
    sb_append(sb, s, p);
}

// Optparse

int optind, optopt;
int opterr = 1;
char *optarg;

int optparse(int argc, char *const argv[], const struct option *opts) {
#define ERROR(...)  do { if (opterr) fprintf(stderr, __VA_ARGS__); } while (0)
  if (optind == 0) {
    optind = 1;
  }

  if (optind >= argc)
    return -1;

  optarg = NULL;
  optopt = 0;

  char *arg = argv[optind];
  char *p = arg;
  if (*p != '-')
    return -1;

  p += 1;
  ++optind;
  for (; opts->name != NULL; ++opts) {
    size_t len = strlen(opts->name);
    if (strncmp(p, opts->name, len) == 0) {
      int opt = opts->val;
      if (opt == 0)
        opt = arg[1];
      char *q = p + len;
      char c = *q;
      if (opts->has_arg) {
        if (c != '\0') {
          optarg = q + (c == '=' ? 1 : 0);
        } else if (optind < argc) {
          optarg = argv[optind++];
        } else {
          ERROR("%s: option '--%s' requires an argument\n", argv[0], opts->name);
          break;
        }
      } else {
        if (c != '\0') {
          if (c != '=')
            continue;
          ERROR("%s: option '--%s' doesn't allow an argument\n", argv[0], opts->name);
          break;
        }
      }
      return opt;
    }
  }

  optopt = arg[1];
  return '?';
#undef ERROR
}
