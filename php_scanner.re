/*
   +----------------------------------------------------------------------+
   | Zend Engine                                                          |
   +----------------------------------------------------------------------+
   | Copyright (c) 1998-2009 Zend Technologies Ltd. (http://www.zend.com) |
   +----------------------------------------------------------------------+
   | This source file is subject to version 2.00 of the Zend license,     |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.zend.com/license/2_00.txt.                                |
   | If you did not receive a copy of the Zend license and are unable to  |
   | obtain it through the world-wide-web, please send a note to          |
   | license@zend.com so we can mail you a copy immediately.              |
   +----------------------------------------------------------------------+
   | Authors: Marcus Boerger <helly@php.net>                              |
   |          Nuno Lopes <nlopess@php.net>                                |
   |          Scott MacVicar <scottmac@php.net>                           |
   | Flex version authors:                                                |
   |          Andi Gutmans <andi@zend.com>                                |
   |          Zeev Suraski <zeev@zend.com>                                |
   +----------------------------------------------------------------------+
*/

/* $Id: php_scanner_language_scanner.l 279941 2009-05-05 01:35:44Z mattwil $ */

#if 0
# define YYDEBUG(s, c) printf("state: %d char: %c\n", s, c)
#else
# define YYDEBUG(s, c)
#endif

#include "php_scanner_defs.h"

#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <string.h>
#include "php_scanner.h"

#define YYCTYPE   unsigned char
#define YYFILL(n) { \
    if (SCNG(yyfill_cb)) { \
        int retval = (SCNG(yyfill_cb))(n, scanner_ctx); \
        if (retval >= 0) \
            return retval; \
    } \
}
#define YYCURSOR  SCNG(yy_cursor)
#define YYLIMIT   SCNG(yy_limit)
#define YYMARKER  SCNG(yy_marker)

#define YYGETCONDITION()  SCNG(yy_state)
#define YYSETCONDITION(s) SCNG(yy_state) = s

#define STATE(name)  yyc##name

/* emulate flex constructs */
#define BEGIN(state) YYSETCONDITION(STATE(state))
#define YYSTATE      YYGETCONDITION()
#define yytext       ((char*)SCNG(yy_text))
#define yyleng       SCNG(yy_leng)
#define yyless(x)    do { YYCURSOR = (unsigned char*)yytext + x; \
                          yyleng   = (unsigned int)x; } while(0)
#define yymore()     goto yymore_restart

/*!max:re2c */
#ifdef HAVE_STDARG_H
# include <stdarg.h>
#endif

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#define SCNG(x) scanner_ctx->x 

#define HANDLE_NEWLINES(s, l)                                                    \
do {                                                                            \
    char *p = (s), *boundary = p+(l);                                            \
                                                                                \
    while (p<boundary) {                                                        \
        if (*p == '\n' || (*p == '\r' && (*(p+1) != '\n'))) {                    \
            SCNG(lineno)++;                                                    \
        }                                                                        \
        p++;                                                                    \
    }                                                                            \
} while (0)

#define HANDLE_NEWLINE(c) \
{ \
    if (c == '\n' || c == '\r') { \
        SCNG(lineno)++; \
    } \
}

/* To save initial string length after scanning to first variable, SCNG(doc_comment_len) can be reused */
#define SET_DOUBLE_QUOTES_SCANNED_LENGTH(len) SCNG(doc_comment_len) = (len)
#define GET_DOUBLE_QUOTES_SCANNED_LENGTH()    SCNG(doc_comment_len)
#define RESET_DOC_COMMENT() { \
    SCNG(doc_comment) = NULL; \
    SCNG(doc_comment_len) = 0; \
}


#define IS_LABEL_START(c) (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z') || (c) == '_' || (c) >= 0x7F)

#define PHP_SCANNER_IS_OCT(c)  ((c)>='0' && (c)<='7')
#define PHP_SCANNER_IS_HEX(c)  (((c)>='0' && (c)<='9') || ((c)>='a' && (c)<='f') || ((c)>='A' && (c)<='F'))

#define STACK_BLOCK_SIZE 64

static int php_scanner_stack_init(php_scanner_stack_t *stack)
{
    stack->top = 0;
    Newx(stack->elements, STACK_BLOCK_SIZE, int);
    stack->max = STACK_BLOCK_SIZE;
    return SUCCESS;
}

static int php_scanner_stack_push(php_scanner_stack_t *stack, const int element)
{
    if (stack->top >= stack->max) {     /* we need to allocate more memory */
        Renew(stack->elements, stack->max += STACK_BLOCK_SIZE, int);
    }
    stack->elements[stack->top] = element;
    return stack->top++;
}

static int php_scanner_stack_pop(php_scanner_stack_t *stack, int *element)
{
    if (stack->top > 0) {
        --stack->top;
        *element = stack->elements[stack->top];
        return SUCCESS;
    } else {
        *element = 0;
        return FAILURE;
    }
}

static int php_scanner_stack_is_empty(const php_scanner_stack_t *stack)
{
    if (stack->top == 0) {
        return 1;
    } else {
        return 0;
    }
}

static int php_scanner_stack_destroy(php_scanner_stack_t *stack)
{
    int i;

    if (stack->elements) {
       Safefree(stack->elements);
    }

    return SUCCESS;
}

static void _yy_push_state(int new_state, php_scanner_ctx_t *scanner_ctx)
{
    php_scanner_stack_push(&SCNG(state_stack), YYGETCONDITION());
    YYSETCONDITION(new_state);
}

#define php_scanner_error croak

#define yy_push_state(state) _yy_push_state(yyc##state, scanner_ctx)

static void yy_pop_state(php_scanner_ctx_t *scanner_ctx)
{
    int stack_state;
    php_scanner_stack_pop(&SCNG(state_stack), &stack_state);
    YYSETCONDITION(stack_state);
}

void php_scanner_set_buffer(char *str, size_t len, int state, php_scanner_ctx_t *scanner_ctx)
{
    YYCURSOR       = YYMARKER = (YYCTYPE*)str;
    SCNG(yy_start) = YYCURSOR;
    YYLIMIT        = YYCURSOR + len;
    YYSETCONDITION(state);
}

void php_scanner_relocate_buffer(char *str, size_t len, php_scanner_ctx_t *scanner_ctx)
{
    size_t off_cursor = YYCURSOR - YYMARKER;
    YYMARKER = SCNG(yy_start) = str;
    YYLIMIT = str + len;
    YYCURSOR = str + off_cursor;
}

void php_scanner_startup(php_scanner_ctx_t *scanner_ctx)
{
    SCNG(heredoc) = NULL;
    SCNG(heredoc_len) = 0;
    SCNG(doc_comment) = NULL;
    SCNG(doc_comment_len) = 0;
    SCNG(lineno) = 1;
    SCNG(increment_lineno) = 0;
    SCNG(filename) = NULL;
    SCNG(current_namespace) = NULL;
    SCNG(current_class_name) = NULL;
    SCNG(current_func_name) = NULL;
    SCNG(short_tags) = 0;
    SCNG(asp_tags) = 0;
    YYSETCONDITION(STATE(INITIAL));
    php_scanner_stack_init(&SCNG(state_stack));
}

void php_scanner_shutdown(php_scanner_ctx_t *scanner_ctx)
{
    if (SCNG(heredoc)) {
        SCNG(heredoc_len)=0;
    }
    php_scanner_stack_destroy(&SCNG(state_stack));
    RESET_DOC_COMMENT();
}

static void php_scanner_scan_escape_string(SV **lv, char *str, size_t len, char quote_type, php_scanner_ctx_t *scanner_ctx)
{
    register char *s, *t;
    char *end;
    STRLEN l;

    *lv = newSVpvn(str, len);

    /* convert escape sequences */
    s = SvPV(*lv, l);
    t = s;
    end = s + l;
    while (s<end) {
        if (*s=='\\') {
            s++;
            if (s >= end) {
                *t++ = '\\';
                break;
            }

            switch(*s) {
                case 'n':
                    *t++ = '\n';
                    l--;
                    break;
                case 'r':
                    *t++ = '\r';
                    l--;
                    break;
                case 't':
                    *t++ = '\t';
                    l--;
                    break;
                case 'f':
                    *t++ = '\f';
                    l--;
                    break;
                case 'v':
                    *t++ = '\v';
                    l--;
                    break;
                case '"':
                case '`':
                    if (*s != quote_type) {
                        *t++ = '\\';
                        *t++ = *s;
                        break;
                    }
                case '\\':
                case '$':
                    *t++ = *s;
                    l--;
                    break;
                case 'x':
                case 'X':
                    if (PHP_SCANNER_IS_HEX(*(s+1))) {
                        char hex_buf[3] = { 0, 0, 0 };

                        l--; /* for the 'x' */

                        hex_buf[0] = *(++s);
                        l--;
                        if (PHP_SCANNER_IS_HEX(*(s+1))) {
                            hex_buf[1] = *(++s);
                            l--;
                        }
                        *t++ = (char) strtol(hex_buf, NULL, 16);
                    } else {
                        *t++ = '\\';
                        *t++ = *s;
                    }
                    break;
                default:
                    /* check for an octal */
                    if (PHP_SCANNER_IS_OCT(*s)) {
                        char octal_buf[4] = { 0, 0, 0, 0 };

                        octal_buf[0] = *s;
                        l--;
                        if (PHP_SCANNER_IS_OCT(*(s+1))) {
                            octal_buf[1] = *(++s);
                            l--;
                            if (PHP_SCANNER_IS_OCT(*(s+1))) {
                                octal_buf[2] = *(++s);
                                l--;
                            }
                        }
                        *t++ = (char) strtol(octal_buf, NULL, 8);
                    } else {
                        *t++ = '\\';
                        *t++ = *s;
                    }
                    break;
            }
        } else {
            *t++ = *s;
        }

        if (*s == '\n' || (*s == '\r' && (*(s+1) != '\n'))) {
            SCNG(lineno)++;
        }
        s++;
    }
    *t = 0;
    SvCUR_set(*lv, l);
}


enum php_scanner_token_e php_scanner_scan(SV **lv, php_scanner_ctx_t *scanner_ctx)
{
restart:
    SCNG(yy_text) = YYCURSOR;

yymore_restart:

/*!re2c
re2c:yyfill:check = 0;
LNUM    [0-9]+
DNUM    ([0-9]*"."[0-9]+)|([0-9]+"."[0-9]*)
EXPONENT_DNUM    (({LNUM}|{DNUM})[eE][+-]?{LNUM})
HNUM    "0x"[0-9a-fA-F]+
LABEL    [a-zA-Z_\x7f-\xff][a-zA-Z0-9_\x7f-\xff]*
WHITESPACE [ \n\r\t]+
TABS_AND_SPACES [ \t]*
TOKENS [;:,.\[\]()|^&+-/*=%!~$<>?@]
ANY_CHAR [^]
NEWLINE ("\r"|"\n"|"\r\n")

/* compute yyleng before each rule */
<!*> := yyleng = YYCURSOR - SCNG(yy_text);


<ST_IN_SCRIPTING>"exit" {
    return T_EXIT;
}

<ST_IN_SCRIPTING>"die" {
    return T_EXIT;
}

<ST_IN_SCRIPTING>"function" {
    return T_FUNCTION;
}

<ST_IN_SCRIPTING>"const" {
    return T_CONST;
}

<ST_IN_SCRIPTING>"return" {
    return T_RETURN;
}

<ST_IN_SCRIPTING>"try" {
    return T_TRY;
}

<ST_IN_SCRIPTING>"catch" {
    return T_CATCH;
}

<ST_IN_SCRIPTING>"throw" {
    return T_THROW;
}

<ST_IN_SCRIPTING>"if" {
    return T_IF;
}

<ST_IN_SCRIPTING>"elseif" {
    return T_ELSEIF;
}

<ST_IN_SCRIPTING>"endif" {
    return T_ENDIF;
}

<ST_IN_SCRIPTING>"else" {
    return T_ELSE;
}

<ST_IN_SCRIPTING>"while" {
    return T_WHILE;
}

<ST_IN_SCRIPTING>"endwhile" {
    return T_ENDWHILE;
}

<ST_IN_SCRIPTING>"do" {
    return T_DO;
}

<ST_IN_SCRIPTING>"for" {
    return T_FOR;
}

<ST_IN_SCRIPTING>"endfor" {
    return T_ENDFOR;
}

<ST_IN_SCRIPTING>"foreach" {
    return T_FOREACH;
}

<ST_IN_SCRIPTING>"endforeach" {
    return T_ENDFOREACH;
}

<ST_IN_SCRIPTING>"declare" {
    return T_DECLARE;
}

<ST_IN_SCRIPTING>"enddeclare" {
    return T_ENDDECLARE;
}

<ST_IN_SCRIPTING>"instanceof" {
    return T_INSTANCEOF;
}

<ST_IN_SCRIPTING>"as" {
    return T_AS;
}

<ST_IN_SCRIPTING>"switch" {
    return T_SWITCH;
}

<ST_IN_SCRIPTING>"endswitch" {
    return T_ENDSWITCH;
}

<ST_IN_SCRIPTING>"case" {
    return T_CASE;
}

<ST_IN_SCRIPTING>"default" {
    return T_DEFAULT;
}

<ST_IN_SCRIPTING>"break" {
    return T_BREAK;
}

<ST_IN_SCRIPTING>"continue" {
    return T_CONTINUE;
}

<ST_IN_SCRIPTING>"goto" {
    return T_GOTO;
}

<ST_IN_SCRIPTING>"echo" {
    return T_ECHO;
}

<ST_IN_SCRIPTING>"print" {
    return T_PRINT;
}

<ST_IN_SCRIPTING>"class" {
    return T_CLASS;
}

<ST_IN_SCRIPTING>"interface" {
    return T_INTERFACE;
}

<ST_IN_SCRIPTING>"extends" {
    return T_EXTENDS;
}

<ST_IN_SCRIPTING>"implements" {
    return T_IMPLEMENTS;
}

<ST_IN_SCRIPTING>"->" {
    yy_push_state(ST_LOOKING_FOR_PROPERTY);
    return T_OBJECT_OPERATOR;
}

<ST_IN_SCRIPTING,ST_LOOKING_FOR_PROPERTY>{WHITESPACE}+ {
    *lv = newSVpvn(yytext, yyleng);
    HANDLE_NEWLINES(yytext, yyleng);
    return T_WHITESPACE;
}

<ST_LOOKING_FOR_PROPERTY>"->" {
    return T_OBJECT_OPERATOR;
}

<ST_LOOKING_FOR_PROPERTY>{LABEL} {
    yy_pop_state(scanner_ctx);
    *lv = newSVpvn(yytext, yyleng);
    return T_STRING;
}

<ST_LOOKING_FOR_PROPERTY>{ANY_CHAR} {
    yyless(0);
    yy_pop_state(scanner_ctx);
    goto restart;
}

<ST_IN_SCRIPTING>"::" {
    return T_PAAMAYIM_NEKUDOTAYIM;
}

<ST_IN_SCRIPTING>"\\" {
    return T_NS_SEPARATOR;
}

<ST_IN_SCRIPTING>"new" {
    return T_NEW;
}

<ST_IN_SCRIPTING>"clone" {
    return T_CLONE;
}

<ST_IN_SCRIPTING>"var" {
    return T_VAR;
}

<ST_IN_SCRIPTING>"("{TABS_AND_SPACES}("int"|"integer"){TABS_AND_SPACES}")" {
    return T_INT_CAST;
}

<ST_IN_SCRIPTING>"("{TABS_AND_SPACES}("real"|"double"|"float"){TABS_AND_SPACES}")" {
    return T_DOUBLE_CAST;
}

<ST_IN_SCRIPTING>"("{TABS_AND_SPACES}"string"{TABS_AND_SPACES}")" {
    return T_STRING_CAST;
}

<ST_IN_SCRIPTING>"("{TABS_AND_SPACES}"binary"{TABS_AND_SPACES}")" {
    return T_STRING_CAST;
}

<ST_IN_SCRIPTING>"("{TABS_AND_SPACES}"array"{TABS_AND_SPACES}")" {
    return T_ARRAY_CAST;
}

<ST_IN_SCRIPTING>"("{TABS_AND_SPACES}"object"{TABS_AND_SPACES}")" {
    return T_OBJECT_CAST;
}

<ST_IN_SCRIPTING>"("{TABS_AND_SPACES}("bool"|"boolean"){TABS_AND_SPACES}")" {
    return T_BOOL_CAST;
}

<ST_IN_SCRIPTING>"("{TABS_AND_SPACES}("unset"){TABS_AND_SPACES}")" {
    return T_UNSET_CAST;
}

<ST_IN_SCRIPTING>"eval" {
    return T_EVAL;
}

<ST_IN_SCRIPTING>"include" {
    return T_INCLUDE;
}

<ST_IN_SCRIPTING>"include_once" {
    return T_INCLUDE_ONCE;
}

<ST_IN_SCRIPTING>"require" {
    return T_REQUIRE;
}

<ST_IN_SCRIPTING>"require_once" {
    return T_REQUIRE_ONCE;
}

<ST_IN_SCRIPTING>"namespace" {
    return T_NAMESPACE;
}

<ST_IN_SCRIPTING>"use" {
    return T_USE;
}

<ST_IN_SCRIPTING>"global" {
    return T_GLOBAL;
}

<ST_IN_SCRIPTING>"isset" {
    return T_ISSET;
}

<ST_IN_SCRIPTING>"empty" {
    return T_EMPTY;
}

<ST_IN_SCRIPTING>"__halt_compiler" {
    return T_HALT_COMPILER;
}

<ST_IN_SCRIPTING>"static" {
    return T_STATIC;
}

<ST_IN_SCRIPTING>"abstract" {
    return T_ABSTRACT;
}

<ST_IN_SCRIPTING>"final" {
    return T_FINAL;
}

<ST_IN_SCRIPTING>"private" {
    return T_PRIVATE;
}

<ST_IN_SCRIPTING>"protected" {
    return T_PROTECTED;
}

<ST_IN_SCRIPTING>"public" {
    return T_PUBLIC;
}

<ST_IN_SCRIPTING>"unset" {
    return T_UNSET;
}

<ST_IN_SCRIPTING>"=>" {
    return T_DOUBLE_ARROW;
}

<ST_IN_SCRIPTING>"list" {
    return T_LIST;
}

<ST_IN_SCRIPTING>"array" {
    return T_ARRAY;
}

<ST_IN_SCRIPTING>"++" {
    return T_INC;
}

<ST_IN_SCRIPTING>"--" {
    return T_DEC;
}

<ST_IN_SCRIPTING>"===" {
    return T_IS_IDENTICAL;
}

<ST_IN_SCRIPTING>"!==" {
    return T_IS_NOT_IDENTICAL;
}

<ST_IN_SCRIPTING>"==" {
    return T_IS_EQUAL;
}

<ST_IN_SCRIPTING>"!="|"<>" {
    return T_IS_NOT_EQUAL;
}

<ST_IN_SCRIPTING>"<=" {
    return T_IS_SMALLER_OR_EQUAL;
}

<ST_IN_SCRIPTING>">=" {
    return T_IS_GREATER_OR_EQUAL;
}

<ST_IN_SCRIPTING>"+=" {
    return T_PLUS_EQUAL;
}

<ST_IN_SCRIPTING>"-=" {
    return T_MINUS_EQUAL;
}

<ST_IN_SCRIPTING>"*=" {
    return T_MUL_EQUAL;
}

<ST_IN_SCRIPTING>"/=" {
    return T_DIV_EQUAL;
}

<ST_IN_SCRIPTING>".=" {
    return T_CONCAT_EQUAL;
}

<ST_IN_SCRIPTING>"%=" {
    return T_MOD_EQUAL;
}

<ST_IN_SCRIPTING>"<<=" {
    return T_SL_EQUAL;
}

<ST_IN_SCRIPTING>">>=" {
    return T_SR_EQUAL;
}

<ST_IN_SCRIPTING>"&=" {
    return T_AND_EQUAL;
}

<ST_IN_SCRIPTING>"|=" {
    return T_OR_EQUAL;
}

<ST_IN_SCRIPTING>"^=" {
    return T_XOR_EQUAL;
}

<ST_IN_SCRIPTING>"||" {
    return T_BOOLEAN_OR;
}

<ST_IN_SCRIPTING>"&&" {
    return T_BOOLEAN_AND;
}

<ST_IN_SCRIPTING>"OR" {
    return T_LOGICAL_OR;
}

<ST_IN_SCRIPTING>"AND" {
    return T_LOGICAL_AND;
}

<ST_IN_SCRIPTING>"XOR" {
    return T_LOGICAL_XOR;
}

<ST_IN_SCRIPTING>"<<" {
    return T_SL;
}

<ST_IN_SCRIPTING>">>" {
    return T_SR;
}

<ST_IN_SCRIPTING>{TOKENS} {
    return yytext[0];
}


<ST_IN_SCRIPTING>"{" {
    yy_push_state(ST_IN_SCRIPTING);
    return '{';
}


<ST_DOUBLE_QUOTES,ST_BACKQUOTE,ST_HEREDOC>"${" {
    yy_push_state(ST_LOOKING_FOR_VARNAME);
    return T_DOLLAR_OPEN_CURLY_BRACES;
}


<ST_IN_SCRIPTING>"}" {
    RESET_DOC_COMMENT();
    if (!php_scanner_stack_is_empty(&SCNG(state_stack))) {
        yy_pop_state(scanner_ctx);
    }
    return '}';
}


<ST_LOOKING_FOR_VARNAME>{LABEL} {
    *lv = newSVpvn(yytext, yyleng);
    yy_pop_state(scanner_ctx);
    yy_push_state(ST_IN_SCRIPTING);
    return T_STRING_VARNAME;
}


<ST_LOOKING_FOR_VARNAME>{ANY_CHAR} {
    yyless(0);
    yy_pop_state(scanner_ctx);
    yy_push_state(ST_IN_SCRIPTING);
    goto restart;
}


<ST_IN_SCRIPTING>{LNUM} {
    *lv = newSVpvn(yytext, yyleng);
    return T_LNUMBER;
}

<ST_IN_SCRIPTING>{HNUM} {
    char *hex = yytext + 2; /* Skip "0x" */
    int len = yyleng - 2;

    /* Skip any leading 0s */
    while (*hex == '0') {
        hex++;
        len--;
    }

    *lv = newSVpvn(hex, len);
}

<ST_VAR_OFFSET>[0]|([1-9][0-9]*) { /* Offset could be treated as a long */
    *lv = newSVpvn(yytext, yyleng);
    return T_NUM_STRING;
}

<ST_VAR_OFFSET>{LNUM}|{HNUM} { /* Offset must be treated as a string */
    *lv = newSVpvn(yytext, yyleng);
    return T_NUM_STRING;
}

<ST_IN_SCRIPTING>{DNUM}|{EXPONENT_DNUM} {
    *lv = newSVpvn(yytext, yyleng);
    return T_DNUMBER;
}

<ST_IN_SCRIPTING>"__CLASS__" {
    const char *class_name = SCNG(current_class_name);
    *lv = newSVpv(class_name ? class_name: "", 0);
    return T_CLASS_C;
}

<ST_IN_SCRIPTING>"__FUNCTION__" {
    const char *func_name = SCNG(current_func_name);
    *lv = newSVpv(func_name ? func_name: "", 0);
    return T_FUNC_C;
}

<ST_IN_SCRIPTING>"__METHOD__" {
    const char *class_name = SCNG(current_class_name);
    const char *func_name = SCNG(current_func_name);
    size_t len = 0;

    if (class_name) {
        len += strlen(class_name) + 2;
    }
    if (func_name) {
        len += strlen(func_name);
    }

    *lv = newSVpv(class_name ? class_name: "", 0);
    sv_catpv(*lv, class_name && func_name ? "::" : "");
    sv_catpv(*lv, func_name ? func_name : "");
    return T_METHOD_C;
}

<ST_IN_SCRIPTING>"__LINE__" {
    *lv = newSViv(SCNG(lineno));
    return T_LINE;
}

<ST_IN_SCRIPTING>"__FILE__" {
    *lv = newSVpv(SCNG(filename) ? SCNG(filename): "", 0);
    return T_FILE;
}

<ST_IN_SCRIPTING>"__DIR__" {
    char *filename, *dir;
    const size_t filename_len = strlen(filename);

    if (!filename) {
        filename = "";
    }

    dir = malloc(filename_len);
    memcpy(filename, SCNG(filename), filename_len);
    filename[filename_len] = '\0';
    dirname(dir);

    if (strcmp(dir, ".") == 0) {
        dir = realloc(dir, MAXPATHLEN);
        getcwd(dir, MAXPATHLEN);
    }

    *lv = newSVpv(dir, 0);
    free(dir);
    return T_DIR;
}

<ST_IN_SCRIPTING>"__NAMESPACE__" {
    if (SCNG(current_namespace)) {
        *lv = newSVpv(SCNG(current_namespace), 0);
    } else {
        *lv = newSVpv("", 0);
    }
    return T_NS_C;
}

<INITIAL>"<script"{WHITESPACE}+"language"{WHITESPACE}*"="{WHITESPACE}*("php"|"\"php\""|"'php'"){WHITESPACE}*">" {
    YYCTYPE *bracket = (YYCTYPE*)memrchr(yytext, '<', yyleng - (sizeof("script language=php>") - 1));

    if (bracket != SCNG(yy_text)) {
        /* Handle previously scanned HTML, as possible <script> tags found are assumed to not be PHP's */
        YYCURSOR = bracket;
        goto inline_html;
    }

    HANDLE_NEWLINES(yytext, yyleng);
    *lv = newSVpvn(yytext, yyleng);
    BEGIN(ST_IN_SCRIPTING);
    return T_OPEN_TAG;
}


<INITIAL>"<%=" {
    if (SCNG(asp_tags)) {
        *lv = newSVpvn(yytext, yyleng);
        BEGIN(ST_IN_SCRIPTING);
        return T_OPEN_TAG_WITH_ECHO;
    } else {
        goto inline_char_handler;
    }
}


<INITIAL>"<?=" {
    if (SCNG(short_tags)) {
        *lv = newSVpvn(yytext, yyleng);
        BEGIN(ST_IN_SCRIPTING);
        return T_OPEN_TAG_WITH_ECHO;
    } else {
        goto inline_char_handler;
    }
}


<INITIAL>"<%" {
    if (SCNG(asp_tags)) {
        *lv = newSVpvn(yytext, yyleng);
        BEGIN(ST_IN_SCRIPTING);
        return T_OPEN_TAG;
    } else {
        goto inline_char_handler;
    }
}


<INITIAL>"<?php"([ \t]|{NEWLINE}) {
    *lv = newSVpvn(yytext, yyleng);
    HANDLE_NEWLINE(yytext[yyleng-1]);
    BEGIN(ST_IN_SCRIPTING);
    return T_OPEN_TAG;
}


<INITIAL>"<?" {
    if (SCNG(short_tags)) {
        *lv = newSVpvn(yytext, yyleng);
        BEGIN(ST_IN_SCRIPTING);
        return T_OPEN_TAG;
    } else {
        goto inline_char_handler;
    }
}

<INITIAL>{ANY_CHAR} {
    if (YYCURSOR > YYLIMIT) {
        return 0;
    }

inline_char_handler:

    while (1) {
        YYCTYPE *ptr = memchr(YYCURSOR, '<', YYLIMIT - YYCURSOR);

        YYCURSOR = ptr ? ptr + 1 : YYLIMIT;

        if (YYCURSOR < YYLIMIT) {
            switch (*YYCURSOR) {
                case '?':
                    if (SCNG(short_tags) || !strncasecmp(YYCURSOR + 1, "php", 3)) { /* Assume [ \t\n\r] follows "php" */
                        break;
                    }
                    continue;
                case '%':
                    if (SCNG(asp_tags)) {
                        break;
                    }
                    continue;
                case 's':
                case 'S':
                    /* Probably NOT an opening PHP <script> tag, so don't end the HTML chunk yet
                     * If it is, the PHP <script> tag rule checks for any HTML scanned before it */
                    YYCURSOR--;
                    yymore();
                default:
                    continue;
            }

            YYCURSOR--;
        }

        break;
    }

inline_html:
    yyleng = YYCURSOR - SCNG(yy_text);

    *lv = newSVpvn(yytext, yyleng);
    HANDLE_NEWLINES(yytext, yyleng);
    return T_INLINE_HTML;
}


/* Make sure a label character follows "->", otherwise there is no property
 * and "->" will be taken literally
 */
<ST_DOUBLE_QUOTES,ST_HEREDOC,ST_BACKQUOTE>"$"{LABEL}"->"[a-zA-Z_\x7f-\xff] {
    yyless(yyleng - 3);
    yy_push_state(ST_LOOKING_FOR_PROPERTY);
    *lv = newSVpvn(yytext + 1, yyleng - 1);
    return T_VARIABLE;
}

/* A [ always designates a variable offset, regardless of what follows
 */
<ST_DOUBLE_QUOTES,ST_HEREDOC,ST_BACKQUOTE>"$"{LABEL}"[" {
    yyless(yyleng - 1);
    yy_push_state(ST_VAR_OFFSET);
    *lv = newSVpvn(yytext + 1, yyleng - 1);
    return T_VARIABLE;
}

<ST_IN_SCRIPTING,ST_DOUBLE_QUOTES,ST_HEREDOC,ST_BACKQUOTE,ST_VAR_OFFSET>"$"{LABEL} {
    *lv = newSVpvn(yytext + 1, yyleng - 1);
    return T_VARIABLE;
}

<ST_VAR_OFFSET>"]" {
    yy_pop_state(scanner_ctx);
    return ']';
}

<ST_VAR_OFFSET>{TOKENS}|[{}"`] {
    /* Only '[' can be valid, but returning other tokens will allow a more explicit parse error */
    return yytext[0];
}

<ST_VAR_OFFSET>[ \n\r\t\\'#] {
    /* Invalid rule to return a more explicit parse error with proper line number */
    yyless(0);
    yy_pop_state(scanner_ctx);
    return T_ENCAPSED_AND_WHITESPACE;
}

<ST_IN_SCRIPTING,ST_VAR_OFFSET>{LABEL} {
    *lv = newSVpvn(yytext, yyleng);
    return T_STRING;
}


<ST_IN_SCRIPTING>"#"|"//" {
    while (YYCURSOR < YYLIMIT) {
        switch (*YYCURSOR++) {
            case '\r':
                if (*YYCURSOR == '\n') {
                    YYCURSOR++;
                }
                /* fall through */
            case '\n':
                SCNG(lineno)++;
                break;
            case '%':
                if (!SCNG(asp_tags)) {
                    continue;
                }
                /* fall through */
            case '?':
                if (*YYCURSOR == '>') {
                    YYCURSOR--;
                    break;
                }
                /* fall through */
            default:
                continue;
        }

        break;
    }

    yyleng = YYCURSOR - SCNG(yy_text);

    return T_COMMENT;
}

<ST_IN_SCRIPTING>"/*"|"/**"{WHITESPACE} {
    int doc_com;

    if (yyleng > 2) {
        doc_com = 1;
        RESET_DOC_COMMENT();
    } else {
        doc_com = 0;
    }

    while (YYCURSOR < YYLIMIT) {
        if (*YYCURSOR++ == '*' && *YYCURSOR == '/') {
            break;
        }
    }

    if (YYCURSOR < YYLIMIT) {
        YYCURSOR++;
    } else {
        php_scanner_error("Unterminated comment starting line %d", SCNG(lineno));
    }

    yyleng = YYCURSOR - SCNG(yy_text);
    HANDLE_NEWLINES(yytext, yyleng);

    if (doc_com) {
        SCNG(doc_comment) = yytext;
        SCNG(doc_comment_len) = yyleng;
        return T_DOC_COMMENT;
    }

    return T_COMMENT;
}

<ST_IN_SCRIPTING>("?>"|"</script"{WHITESPACE}*">"){NEWLINE}? {
    *lv = newSVpvn(yytext, yyleng);
    BEGIN(INITIAL);
    return T_CLOSE_TAG;  /* implicit ';' at php-end tag */
}


<ST_IN_SCRIPTING>"%>"{NEWLINE}? {
    if (SCNG(asp_tags)) {
        BEGIN(INITIAL);
        *lv = newSVpvn(yytext, yyleng);
        return T_CLOSE_TAG;  /* implicit ';' at php-end tag */
    } else {
        yyless(1);
        return yytext[0];
    }
}


<ST_IN_SCRIPTING>b?['] {
    register char *s, *t;
    STRLEN l;
    char *end;
    int bprefix = (yytext[0] != '\'') ? 1 : 0;

    while (1) {
        if (YYCURSOR < YYLIMIT) {
            if (*YYCURSOR == '\'') {
                YYCURSOR++;
                yyleng = YYCURSOR - SCNG(yy_text);

                break;
            } else if (*YYCURSOR++ == '\\' && YYCURSOR < YYLIMIT) {
                YYCURSOR++;
            }
        } else {
            yyleng = YYLIMIT - SCNG(yy_text);

            /* Unclosed single quotes; treat similar to double quotes, but without a separate token
             * for ' (unrecognized by parser), instead of old flex fallback to "Unexpected character..."
             * rule, which continued in ST_IN_SCRIPTING state after the quote */
            return T_ENCAPSED_AND_WHITESPACE;
        }
    }

    *lv = newSVpvn(yytext + bprefix + 1, yyleng - bprefix - 2);

    /* convert escape sequences */
    s = SvPV(*lv, l);
    t = s;
    end = s + l;
    while (s<end) {
        if (*s=='\\') {
            s++;

            switch(*s) {
                case '\\':
                case '\'':
                    *t++ = *s;
                    l--;
                    break;
                default:
                    *t++ = '\\';
                    *t++ = *s;
                    break;
            }
        } else {
            *t++ = *s;
        }

        if (*s == '\n' || (*s == '\r' && (*(s+1) != '\n'))) {
            SCNG(lineno)++;
        }
        s++;
    }
    *t = 0;
    SvCUR_set(*lv, l);

    return T_CONSTANT_ENCAPSED_STRING;
}


<ST_IN_SCRIPTING>b?["] {
    int bprefix = (yytext[0] != '"') ? 1 : 0;

    while (YYCURSOR < YYLIMIT) {
        switch (*YYCURSOR++) {
            case '"':
                yyleng = YYCURSOR - SCNG(yy_text);
                php_scanner_scan_escape_string(lv, yytext+bprefix+1, yyleng-bprefix-2, '"', scanner_ctx);
                return T_CONSTANT_ENCAPSED_STRING;
            case '$':
                if (IS_LABEL_START(*YYCURSOR) || *YYCURSOR == '{') {
                    break;
                }
                continue;
            case '{':
                if (*YYCURSOR == '$') {
                    break;
                }
                continue;
            case '\\':
                if (YYCURSOR < YYLIMIT) {
                    YYCURSOR++;
                }
                /* fall through */
            default:
                continue;
        }

        YYCURSOR--;
        break;
    }

    /* Remember how much was scanned to save rescanning */
    SET_DOUBLE_QUOTES_SCANNED_LENGTH(YYCURSOR - SCNG(yy_text) - yyleng);

    YYCURSOR = SCNG(yy_text) + yyleng;

    BEGIN(ST_DOUBLE_QUOTES);
    return '"';
}


<ST_IN_SCRIPTING>b?"<<<"{TABS_AND_SPACES}({LABEL}|([']{LABEL}['])|(["]{LABEL}["])){NEWLINE} {
    char *s;
    int bprefix = (yytext[0] != '<') ? 1 : 0;

    SCNG(lineno)++;
    SCNG(heredoc_len) = yyleng-bprefix-3-1-(yytext[yyleng-2]=='\r'?1:0);
    s = yytext+bprefix+3;
    while ((*s == ' ') || (*s == '\t')) {
        s++;
        SCNG(heredoc_len)--;
    }

    if (*s == '\'') {
        s++;
        SCNG(heredoc_len) -= 2;

        BEGIN(ST_NOWDOC);
    } else {
        if (*s == '"') {
            s++;
            SCNG(heredoc_len) -= 2;
        }

        BEGIN(ST_HEREDOC);
    }

    SCNG(heredoc) = s;

    /* Check for ending label on the next line */
    if (SCNG(heredoc_len) < YYLIMIT - YYCURSOR && !memcmp(YYCURSOR, s, SCNG(heredoc_len))) {
        YYCTYPE *end = YYCURSOR + SCNG(heredoc_len);

        if (*end == ';') {
            end++;
        }

        if (*end == '\n' || *end == '\r') {
            BEGIN(ST_END_HEREDOC);
        }
    }

    return T_START_HEREDOC;
}


<ST_IN_SCRIPTING>[`] {
    BEGIN(ST_BACKQUOTE);
    return '`';
}


<ST_END_HEREDOC>{ANY_CHAR} {
    YYCURSOR += SCNG(heredoc_len) - 1;
    yyleng = SCNG(heredoc_len);

    *lv = newSVpv(SCNG(heredoc), SCNG(heredoc_len));
    SCNG(heredoc) = NULL;
    SCNG(heredoc_len) = 0;
    BEGIN(ST_IN_SCRIPTING);
    return T_END_HEREDOC;
}


<ST_DOUBLE_QUOTES,ST_BACKQUOTE,ST_HEREDOC>"{$" {
    *lv = newSVpvn(yytext, yyleng);
    yy_push_state(ST_IN_SCRIPTING);
    yyless(1);
    return T_CURLY_OPEN;
}


<ST_DOUBLE_QUOTES>["] {
    BEGIN(ST_IN_SCRIPTING);
    return '"';
}

<ST_BACKQUOTE>[`] {
    BEGIN(ST_IN_SCRIPTING);
    return '`';
}


<ST_DOUBLE_QUOTES>{ANY_CHAR} {
    if (GET_DOUBLE_QUOTES_SCANNED_LENGTH()) {
        YYCURSOR += GET_DOUBLE_QUOTES_SCANNED_LENGTH() - 1;
        SET_DOUBLE_QUOTES_SCANNED_LENGTH(0);

        goto double_quotes_scan_done;
    }

    if (YYCURSOR > YYLIMIT) {
        return 0;
    }
    if (yytext[0] == '\\' && YYCURSOR < YYLIMIT) {
        YYCURSOR++;
    }

    while (YYCURSOR < YYLIMIT) {
        switch (*YYCURSOR++) {
            case '"':
                break;
            case '$':
                if (IS_LABEL_START(*YYCURSOR) || *YYCURSOR == '{') {
                    break;
                }
                continue;
            case '{':
                if (*YYCURSOR == '$') {
                    break;
                }
                continue;
            case '\\':
                if (YYCURSOR < YYLIMIT) {
                    YYCURSOR++;
                }
                /* fall through */
            default:
                continue;
        }

        YYCURSOR--;
        break;
    }

double_quotes_scan_done:
    yyleng = YYCURSOR - SCNG(yy_text);

    php_scanner_scan_escape_string(lv, yytext, yyleng, '"', scanner_ctx);
    return T_ENCAPSED_AND_WHITESPACE;
}


<ST_BACKQUOTE>{ANY_CHAR} {
    if (YYCURSOR > YYLIMIT) {
        return 0;
    }
    if (yytext[0] == '\\' && YYCURSOR < YYLIMIT) {
        YYCURSOR++;
    }

    while (YYCURSOR < YYLIMIT) {
        switch (*YYCURSOR++) {
            case '`':
                break;
            case '$':
                if (IS_LABEL_START(*YYCURSOR) || *YYCURSOR == '{') {
                    break;
                }
                continue;
            case '{':
                if (*YYCURSOR == '$') {
                    break;
                }
                continue;
            case '\\':
                if (YYCURSOR < YYLIMIT) {
                    YYCURSOR++;
                }
                /* fall through */
            default:
                continue;
        }

        YYCURSOR--;
        break;
    }

    yyleng = YYCURSOR - SCNG(yy_text);

    php_scanner_scan_escape_string(lv, yytext, yyleng, '`', scanner_ctx);
    return T_ENCAPSED_AND_WHITESPACE;
}


<ST_HEREDOC>{ANY_CHAR} {
    int newline = 0;

    if (YYCURSOR > YYLIMIT) {
        return 0;
    }

    YYCURSOR--;

    while (YYCURSOR < YYLIMIT) {
        switch (*YYCURSOR++) {
            case '\r':
                if (*YYCURSOR == '\n') {
                    YYCURSOR++;
                }
                /* fall through */
            case '\n':
                /* Check for ending label on the next line */
                if (IS_LABEL_START(*YYCURSOR) && SCNG(heredoc_len) < YYLIMIT - YYCURSOR && !memcmp(YYCURSOR, SCNG(heredoc), SCNG(heredoc_len))) {
                    YYCTYPE *end = YYCURSOR + SCNG(heredoc_len);

                    if (*end == ';') {
                        end++;
                    }

                    if (*end == '\n' || *end == '\r') {
                        /* newline before label will be subtracted from returned text, but
                         * yyleng/yytext will include it, for php_scanner_highlight/strip, tokenizer, etc. */
                        if (YYCURSOR[-2] == '\r' && YYCURSOR[-1] == '\n') {
                            newline = 2; /* Windows newline */
                        } else {
                            newline = 1;
                        }

                        SCNG(increment_lineno) = 1; /* For newline before label */
                        BEGIN(ST_END_HEREDOC);

                        goto heredoc_scan_done;
                    }
                }
                continue;
            case '$':
                if (IS_LABEL_START(*YYCURSOR) || *YYCURSOR == '{') {
                    break;
                }
                continue;
            case '{':
                if (*YYCURSOR == '$') {
                    break;
                }
                continue;
            case '\\':
                if (YYCURSOR < YYLIMIT && *YYCURSOR != '\n' && *YYCURSOR != '\r') {
                    YYCURSOR++;
                }
                /* fall through */
            default:
                continue;
        }

        YYCURSOR--;
        break;
    }

heredoc_scan_done:
    yyleng = YYCURSOR - SCNG(yy_text);

    php_scanner_scan_escape_string(lv, yytext, yyleng - newline, 0, scanner_ctx);
    return T_ENCAPSED_AND_WHITESPACE;
}


<ST_NOWDOC>{ANY_CHAR} {
    int newline = 0;

    if (YYCURSOR > YYLIMIT) {
        return 0;
    }

    YYCURSOR--;

    while (YYCURSOR < YYLIMIT) {
        switch (*YYCURSOR++) {
            case '\r':
                if (*YYCURSOR == '\n') {
                    YYCURSOR++;
                }
                /* fall through */
            case '\n':
                /* Check for ending label on the next line */
                if (IS_LABEL_START(*YYCURSOR) && SCNG(heredoc_len) < YYLIMIT - YYCURSOR && !memcmp(YYCURSOR, SCNG(heredoc), SCNG(heredoc_len))) {
                    YYCTYPE *end = YYCURSOR + SCNG(heredoc_len);

                    if (*end == ';') {
                        end++;
                    }

                    if (*end == '\n' || *end == '\r') {
                        /* newline before label will be subtracted from returned text, but
                         * yyleng/yytext will include it */
                        if (YYCURSOR[-2] == '\r' && YYCURSOR[-1] == '\n') {
                            newline = 2; /* Windows newline */
                        } else {
                            newline = 1;
                        }

                        SCNG(increment_lineno) = 1; /* For newline before label */
                        BEGIN(ST_END_HEREDOC);

                        goto nowdoc_scan_done;
                    }
                }
                /* fall through */
            default:
                continue;
        }
    }

nowdoc_scan_done:
    yyleng = YYCURSOR - SCNG(yy_text);

    *lv = newSVpvn(yytext, yyleng - newline);
    HANDLE_NEWLINES(yytext, yyleng - newline);
    return T_ENCAPSED_AND_WHITESPACE;
}


<ST_IN_SCRIPTING,ST_VAR_OFFSET>{ANY_CHAR} {
    if (YYCURSOR > YYLIMIT) {
        return 0;
    }

    php_scanner_error("Unexpected character in input:  '%c' (ASCII=%d) state=%d", yytext[0], yytext[0], YYSTATE);
    goto restart;
}

*/
}
