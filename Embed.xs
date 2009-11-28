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
	ptrdiff_t off = (char*)sc->yy_cursor - old_bufptr;
	size_t l;
	if (sc->yy_limit - sc->yy_cursor >= n)
		return -1;
	if (!lex_next_chunk(LEX_KEEP_PREVIOUS))
		return -1;
	l = PL_parser->bufend - PL_parser->bufptr;
	if (l - off < n) {
		char *buf = SvPVX(PL_parser->linestr);
		ptrdiff_t bufptr_off = PL_parser->bufptr - buf;
		ptrdiff_t bufend_off = PL_parser->bufend - buf;
		ptrdiff_t oldbufptr_off = PL_parser->oldbufptr - buf;
		ptrdiff_t oldoldbufptr_off = PL_parser->oldoldbufptr - buf;
		ptrdiff_t linestart_off = PL_parser->linestart - buf;
		ptrdiff_t last_uni_off = PL_parser->last_uni ?
					PL_parser->last_uni - buf: 0;
		ptrdiff_t last_lop_off = PL_parser->last_lop ?
					PL_parser->last_lop - buf: 0;
		SvGROW(PL_parser->linestr, off + n + 1);
		buf = SvPVX(PL_parser->linestr);
		PL_parser->bufptr = buf + bufptr_off;
		PL_parser->bufend = buf + bufend_off;
		PL_parser->oldbufptr = buf + oldbufptr_off;
		PL_parser->oldoldbufptr = buf + oldoldbufptr_off;
		PL_parser->linestart = buf + linestart_off;
		PL_parser->last_uni = PL_parser->last_uni ?
					PL_parser->last_uni + last_uni_off:
					0;
		PL_parser->last_lop = PL_parser->last_lop ?
					PL_parser->last_lop + last_lop_off:
					0;
		Zero(PL_parser->bufptr + l, off + n + 1 - l, char);
	}
	php_scanner_relocate_buffer(PL_parser->bufptr + off, PL_parser->bufend - PL_parser->bufptr - off, sc);
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
			SvREFCNT_dec(lv);
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
