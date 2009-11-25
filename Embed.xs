/*
 * vim: noet
 */
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "php_scanner.h"
#include "php_scanner_defs.h"

static int (*next_keyword_plugin)(pTHX_ char *, STRLEN, OP **);

static SV* eval_php_script(const char *script, size_t script_len)
{
    int stdin_fds[2] = { -1, -1 };
    int stdout_fds[2] = { -1, -1 };
    pid_t pid;
    SV *retval = NULL;

    if (pipe(stdin_fds))
        goto out;
    if (pipe(stdout_fds))
        goto out;

    pid = fork();
    if (pid < 0)
        goto out;

    if (pid) {
		int err;
        const char *p = script, *e = script + script_len;
		close(stdin_fds[0]);
		close(stdout_fds[1]);
        while (p < e) {
            ssize_t nbytes_written = write(stdin_fds[1], p, e - p);
            if (nbytes_written < 0)
                goto out;
            p += nbytes_written;
        }
        close(stdin_fds[1]);
        retval = newSVpvn("", 0);
        for (;;) {
            char buf[4096];
            ssize_t nbytes_read = read(stdout_fds[0], buf, sizeof(buf));
            if (nbytes_read < 0) {
                SvREFCNT_dec(retval);
                retval = NULL;
                goto out;
            } else if (nbytes_read == 0) {
                break;
            }
            sv_catpvn(retval, buf, nbytes_read);
        }
        waitpid(pid, &err, 0);
		if (WEXITSTATUS(err)) {
			croak("Subprocess returned error status (%d)", WEXITSTATUS(err));
		}
    } else {
        static char * const args[] = { "php", NULL };
		close(stdin_fds[1]);
		close(stdout_fds[0]);
        dup2(stdin_fds[0], 0);
        dup2(stdout_fds[1], 1);
        if (execvp(args[0], args) < 0)
			exit(255);
    }
out:
    if (stdin_fds[0] >= 0)
        close(stdin_fds[0]);
    if (stdin_fds[1] >= 0)
        close(stdin_fds[1]);
    if (stdout_fds[0] >= 0)
        close(stdout_fds[0]);
    if (stdout_fds[1] >= 0)
        close(stdout_fds[1]);
    return retval;
}

static int yyfill_cb(size_t n, php_scanner_ctx_t *sc)
{
	const char *old_bufptr = PL_parser->bufptr;
	const char *old_bufend = PL_parser->bufend;
	ptrdiff_t off = (char*)sc->yy_marker - old_bufptr;
	if (!lex_next_chunk(sc->yy_marker >= sc->yy_limit ? 0: LEX_KEEP_PREVIOUS))
		return -1;
	php_scanner_relocate_buffer(PL_parser->bufptr + off, PL_parser->bufend - PL_parser->bufptr, sc);
	if (sc->yy_limit - sc->yy_cursor < n)
		sc->yy_limit = sc->yy_cursor + n; /* XXX */
	return -1;
}

static OP *THX_parse_php(pTHX)
{
	php_scanner_ctx_t sc;
	php_scanner_startup(&sc);
	php_scanner_set_buffer(PL_parser->bufptr, PL_parser->bufend - PL_parser->bufptr, yycST_IN_SCRIPTING, &sc);
	sc.yyfill_cb = yyfill_cb;
	for (;;) {
		enum php_scanner_token_e tok;
		SV *lv = NULL;
		tok = php_scanner_scan(&lv, &sc);
		if (!tok) {
			croak("Unexpected EOF");
		}
		if (lv)
			sv_2mortal(lv);
		if (tok == T_CLOSE_TAG)
			break;
	}
	{
		OP *retval;
		HV *stash = gv_stashpvn("PHP::Embed", sizeof("PHP::Embed") - 1, 0);
		assert(stash != NULL);
		GV *gv_php = gv_fetchmeth(stash, "php", sizeof("php") - 1, 0);
		if (!gv_php)
			croak("PHP::Embed::php() not defined");
		{
			SV *scr= newSVpvn(PL_parser->bufptr - 5, ((char *)sc.yy_cursor - PL_parser->bufptr) + 5);
			retval = (OP*)newUNOP(OP_ENTERSUB, OPf_STACKED,
				Perl_append_elem(aTHX_ OP_LIST, newSVOP(OP_CONST, 0, scr),
						newCVREF(0, newGVOP(OP_GV, 0, gv_php))));
		}
		lex_read_to(sc.yy_cursor);
		return retval;
	}
}
#define parse_php() THX_parse_php(aTHX)

static int my_keyword_plugin(pTHX_
	char *keyword_ptr, STRLEN keyword_len, OP **op_ptr)
{
	if(keyword_len == 5 && strnEQ(keyword_ptr, "<?php", 5)) {
		*op_ptr = parse_php();
		return KEYWORD_PLUGIN_EXPR;
	} else {
		return next_keyword_plugin(aTHX_
				keyword_ptr, keyword_len, op_ptr);
	}
}

MODULE = PHP::Embed PACKAGE = PHP::Embed

SV*
php(script)
		SV* script
	CODE:
		STRLEN len;
		char *s = SvPV(script, len);
		RETVAL = eval_php_script(s, len);
	OUTPUT:
		RETVAL

BOOT:
	next_keyword_plugin = PL_keyword_plugin;
	PL_keyword_plugin = my_keyword_plugin;
