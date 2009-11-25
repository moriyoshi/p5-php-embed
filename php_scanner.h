#ifndef PHP_SCANNER_H
#define PHP_SCANNER_H

#include "EXTERN.h"
#include "perl.h"
#include "php_scanner_tokens.h"

enum php_scanner_status_e {
    FAILURE,
    SUCCESS
};

typedef struct php_scanner_stack_t {
	int top, max;
	int *elements;
} php_scanner_stack_t;

typedef struct php_scanner_ctx_t php_scanner_ctx_t;

typedef int (*yyfill_cb_fn_t)(size_t n, php_scanner_ctx_t *);

struct php_scanner_ctx_t {
    unsigned int yy_leng;
    unsigned char *yy_start;
    unsigned char *yy_text;
    unsigned char *yy_cursor;
    unsigned char *yy_marker;
    unsigned char *yy_limit;
    int yy_state;
    php_scanner_stack_t state_stack;

    int lineno;
    int increment_lineno;

    char *heredoc;
    int heredoc_len;

    char *doc_comment;
    unsigned int doc_comment_len;

    const char *filename;

    const char *current_namespace;
    const char *current_class_name;
    const char *current_func_name;

    int short_tags:1;
    int asp_tags:1;

    yyfill_cb_fn_t yyfill_cb;
};

enum php_scanner_token_e php_scanner_scan(SV **lv, php_scanner_ctx_t *scanner_ctx);
void php_scanner_set_buffer(char *str, size_t len, int state, php_scanner_ctx_t *scanner_ctx);
void php_scanner_relocate_buffer(char *str, size_t len, php_scanner_ctx_t *scanner_ctx);

void php_scanner_startup(php_scanner_ctx_t *scanner_ctx);
void php_scanner_shutdown(php_scanner_ctx_t *scanner_ctx);

#endif /* PHP_SCANNER_H */
