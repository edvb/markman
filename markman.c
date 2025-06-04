/* See LICENSE file for copyright and license details. */
#include <ctype.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "arg.h"

#include "config.h"

#define WS " \t\n"

char *argv0;

struct Block;
typedef struct Block *Block;
struct Line;
typedef struct Line *Line;

typedef enum {
	STR,
	BOLD,
	ITALIC,
	LINK,
	LCODE,
} LineType;

typedef enum {
	BR,
	HEADER,
	PARA,
	QUOTE,
	ULIST,
	OLIST,
	BCODE,
} BlockType;

struct Line {
	LineType t;
	union {
		char *s; /* STR, BOLD, ITALIC, LCODE */
		struct { /* LINK */
			Line  txt;
			char *url;
		} u;
	} v;
	Line next;
};

struct Block {
	BlockType t; /* BR */
	union {
		char *s; /* BCODE */
		Line  l; /* PARA, QUOTE, */
		struct { /* HEADER */
			int lvl;
			Line l;
		} h;
	} v;
	Block next;
};

void
die(int eval, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt)-1] == ':')
		fputc(' ', stderr),
		perror(NULL);
	else
		fputc('\n', stderr);

	exit(eval);
}
static char*
str_cap(char *s)
{
	char *p;
	if (cap)
		for (p = s; *p; p++)
			*p = toupper(*p);
	return s;
}

static Line
mk_str(char *s, LineType t)
{
	Line l = malloc(sizeof(struct Line));
	if (!l) die(1, "malloc:");
	l->t = t;
	if (!(l->v.s = strdup(s)))
		die(1, "strdup:");
	l->next = NULL;
	return l;
}

static Block
mk_header(int lvl, Line l)
{
	Block b = malloc(sizeof(struct Block));
	if (!b) die(1, "malloc:");
	b->t = HEADER;
	b->v.h.lvl = lvl;
	b->v.h.l   = l;
	b->next = NULL;
	return b;
}

static Block
mk_para(Line l)
{
	Block b = malloc(sizeof(struct Block));
	if (!b) die(1, "malloc:");
	b->t = PARA;
	b->v.l  = l;
	b->next = NULL;
	return b;
}

static Block
mk_ulist(Line l)
{
	Block b = malloc(sizeof(struct Block));
	if (!b) die(1, "malloc:");
	b->t = ULIST;
	b->v.l  = l;
	b->next = NULL;
	return b;
}

static Block
mk_bcode(char *s)
{
	Block b = malloc(sizeof(struct Block));
	if (!b) die(1, "malloc:");
	b->t = BCODE;
	if (!(b->v.s = strdup(s)))
		die(1, "strdup:");
	b->next = NULL;
	return b;
}

static Line
line_parse(char *src, LineType t)
{
	Line ret;
	char *s;
	for (s = src; *src; src++) /* TODO rewrite this mess */
		if (((*src == '*' && src[1] == '*') ||
		     (*src == '_' && src[1] == '_')) && t != LCODE) {
			*src = '\0';
			ret = mk_str(s, t);
			src += 2;
			ret->next = line_parse(src, t == BOLD ? STR : BOLD);
			return ret;
		} else if ((*src == '*' || *src == '_') && t != LCODE) {
			*src = '\0';
			ret = mk_str(s, t);
			src += 1;
			ret->next = line_parse(src, t == ITALIC ? STR : ITALIC);
			return ret;
		} else if (*src == '`') {
			*src = '\0';
			ret = mk_str(s, t);
			src += 1;
			ret->next = line_parse(src, t == LCODE ? STR : LCODE);
			return ret;
		} else if (*src == '[' && t != LCODE) {
			*src = '\0';
			ret = mk_str(s, t);
			src += 1;
			ret->next = line_parse(src, LINK);
			return ret;
		} else if (*src == ']' && t == LINK && t != LCODE) {
			*src = '\0';
			ret = mk_str(s, t);
			src += 1;
			src += strcspn(src, WS);
			ret->next = line_parse(src, STR);
			return ret;
		} else if (*src == '!' && src[1] == '[' && t != LCODE) {
			*src = '\0';
			ret = mk_str(s, t);
			src += 1;
			src += strcspn(src, "]") + 1;
			src += strcspn(src, "])") + 1;
			ret->next = line_parse(++src, STR);
			return ret;
		}
	return mk_str(s, STR);
}

Block
markman_parse(char *src)
{
	Block ret;
	int lvl = 1, i;
	char *s;
	switch (*src) {
	case '#':
		for (; *src && *(++src) == '#'; lvl++);
		src += strspn(src, WS);
		s = src;
		src += strcspn(src, "\n");
		*src = '\0';
		ret = mk_header(lvl, line_parse(lvl > 2 || (lvl == 1 && namesec)
		                                ? s : str_cap(s), STR));
		if (lvl > 3)
			ret->v.h.l->t = BOLD;
		ret->next = markman_parse(++src);
		return ret;
	case '\n':
		return markman_parse(++src);
	case '\0':
		return NULL;
	case '`':
		if (src[1] == '`' && src[2] == '`')
			src += strcspn(src, "\n");
		for (s = src; *src; src++)
			if (!strncmp(src, "\n```\n", 5)) {
				src[1] = '\0';
				src += 5;
				break;
			}
		ret = mk_bcode(s);
		ret->next = markman_parse(src);
		return ret;
	case '*': /* TODO support -  */
		if (src[1] == ' ') {
			/* TODO fix multiline points */
			for (s = src; *src; src++)
				/* TODO support spaces before list */
				if (!strncmp(src, "\n* ", 3)) {
					src[0] = '\0';
					src += 1;
					break;
				} else if (!strncmp(src, "\n\n", 2)) {
					src[0] = '\0';
					src += 2;
					break;
				}
			ret = mk_ulist(line_parse(s+2, STR));
			ret->next = markman_parse(src);
			return ret;
		}
	default:
		src += strspn(src, WS);
		s = calloc(strlen(src), sizeof(char));
		if (!s) die(1, "calloc:");
		for (i = 0; *src; src++, i++)
			if (*src == '\n' && src[1] == '\n') {
				s[i+1] = '\0';
				break;
			} else if (*src == '\n') {
				s[i] = ' ';
			} else {
				s[i] = *src;
			}
		src += strspn(src, WS);
		ret = mk_para(line_parse(s, STR));
		ret->next = markman_parse(src);
		return ret;
	}
}

void disp_line(Line l)
{
	switch (l->t) {
	case LINK:
	case STR:
		printf(l->v.s);
		break;
	case BOLD:
		printf("\\fB%s\\fP", l->v.s);
		break;
	case ITALIC:
		printf("\\fI%s\\fP", l->v.s);
		break;
	case LCODE:
		printf("'%s'", l->v.s);
		break;
	}
	if (l->next)
		disp_line(l->next);
}

void
disp_block(Block b, Block prev)
{
	switch (b->t) {
	case BR:
		break;
	case HEADER:
		if (b->v.h.lvl == 1 && namesec) break;
		switch (b->v.h.lvl) {
		case 1:
			printf(".TH ");
			break;
		case 2:
			printf(".SH ");
			break;
		case 3:
			printf(".SS ");
			break;
		case 4:
			printf(".TP\n");
			break;
		}
		disp_line(b->v.h.l);
		putchar('\n');
		break;
	case PARA:
		if (prev && prev->t == HEADER && prev->v.h.lvl < 4)
			puts(".PP");
		disp_line(b->v.l);
		puts("\n.PP");
		break;
	case BCODE:
		printf(".RS 4\n.EX\n%s\n.EE\n.RE\n", b->v.s);
		break;
	case ULIST:
		printf(".IP ∙ 2\n");
		disp_line(b->v.l);
		puts("\n.PP");
	case QUOTE:
	case OLIST:
		break;
	}
	if (b->next)
		disp_block(b->next, b);
}

void
markman_disp(Block b, char *name)
{
	if (!b) return;
	if (b->t != HEADER || b->v.h.lvl != 1 || namesec) {
		printf(".TH %s %ld ", str_cap(name), sec);
		if (date) {
			printf("\"%s\" ", date);
		} else {
			/* TODO option to configure date format */
			time_t tt = time(NULL);
			struct tm *t = localtime(&tt);
			printf("%d-%02d-%02d ", t->tm_year+1900, t->tm_mon+1, t->tm_mday);
		}
		if (ver)
			printf("\"%s\" ", ver);
		if (mid)
			printf("\"%s\"", mid);
		putchar('\n');
	}
	if (namesec && b->t == HEADER) {
		puts(".PP\n.SH NAME");
		disp_line(b->v.h.l);
		putchar('\n');
	}
	if (synsec)
		printf(".PP\n.SH SYNOPSIS\n%s\n", synsec);
	if (descsec)
		puts(".PP\n.SH DESCRIPTION");
	disp_block(b, NULL);
}

static char*
str_file(int fd)
{
	char buf[BUFSIZ], *file = NULL;
	int len = 0, n;
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		file = realloc(file, len + n + 1);
		if (!file) die(1, "realloc:");
		memcpy(file + len, buf, n);
		len += n;
		file[len] = '\0';
	}
	if (fd)
		close(fd);
	if (n < 0)
		die(1, "read:");
	return file;
}

static void
usage(const int eval)
{
	die(eval, "usage: %s [-cChv] [-t TITLE] [-d DATE] [-SECNUM] [FILENAME]", argv0);
}

int
main(int argc, char *argv[])
{

	ARGBEGIN {
	case 'c':
		cap = 0;
		break;
	case 'C':
		cap = 1;
		break;
	ARGNUM:
		if ((sec = ARGNUMF()) > 8 || sec == 0)
			die(1, "%s: invalid section number, expect 1-8, received [%d]", argv0, sec);
		break;
	case 't':
		title = EARGF(usage(1));
		break;
	case 'd':
		date = EARGF(usage(1));
		break;
	case 'V':
		ver = EARGF(usage(1));
		break;
	case 'm':
		mid = EARGF(usage(1));
		break;
	case 'n':
		namesec = 1;
		break;
	case 's':
		synsec = EARGF(usage(1));
		break;
	case 'D':
		descsec = 1;
		break;
	case 'h':
		usage(0);
	case 'v':
		die(0, "%s v%s (c) 2017-2022 Ed van Bruggen", argv0, VERSION);
	default:
		usage(1);
	} ARGEND;

	char *file;
	int fd;
	if (*argv) {
		if ((fd = open(*argv, O_RDONLY)) < 0)
			die(1, "%s: %s:", argv0, *argv);
		file = str_file(fd);
		if (!title) title = *argv;
		markman_disp(markman_parse(file), title);
	} else {
		file = str_file(0);
		markman_disp(markman_parse(file), "stdin");
	}
	free(file);
	putchar('\n');

	return EXIT_SUCCESS;
}
