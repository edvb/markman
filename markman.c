/* See LICENSE file for copyright and license details. */
#include <libgen.h>
#include <time.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>

#include "arg.h"
#include "util.h"

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

static char *
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
	Line l = emalloc(sizeof(struct Line));
	l->t = t;
	l->v.s = estrdup(s);
	l->next = NULL;
	return l;
}

static Block
mk_header(int lvl, Line l)
{
	Block b = emalloc(sizeof(struct Block));
	b->t = HEADER;
	b->v.h.lvl = lvl;
	b->v.h.l   = l;
	b->next = NULL;
	return b;
}

static Block
mk_para(Line l)
{
	Block b = emalloc(sizeof(struct Block));
	b->t = PARA;
	b->v.l  = l;
	b->next = NULL;
	return b;
}

static Block
mk_bcode(char *s)
{
	Block b = emalloc(sizeof(struct Block));
	b->t = BCODE;
	b->v.s  = estrdup(s);
	b->next = NULL;
	return b;
}

static Line
line_parse(char *src, LineType t)
{
	Line ret;
	char *s;
	for (s = src; *src; src++) /* TODO rewrite this mess */
		if ((*src == '*' && src[1] == '*') ||
		    (*src == '_' && src[1] == '_')) {
			*src = '\0';
			ret = mk_str(s, t);
			src += 2;
			ret->next = line_parse(src, t == BOLD ? STR : BOLD);
			return ret;
		} else if (*src == '*' || *src == '_') {
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
		} else if (*src == '[') {
			*src = '\0';
			ret = mk_str(s, t);
			src += 1;
			ret->next = line_parse(src, LINK);
			return ret;
		} else if (*src == ']' && t == LINK) {
			*src = '\0';
			ret = mk_str(s, t);
			src += 1;
			src += strcspn(src, WS);
			ret->next = line_parse(src, STR);
			return ret;
		} else if (*src == '!' && src[1] == '[') {
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
		s = ecalloc(strcspn(src, "\n") + 1, sizeof(char));
		for (i = 0; *src && *src != '\n'; src++, i++)
			s[i] = *src;
		ret = mk_header(lvl, line_parse((s[0] != '-') ? str_cap(s) : s, STR));
		if (lvl >= 4)
			ret->v.h.l->t = BOLD;
		free(s);
		ret->next = markman_parse(++src);
		return ret;
	case '\n':
		return markman_parse(++src);
	case '\0':
		return NULL;
	/* case '\t': */
	/* 	s = ++src; */
	/* 	for (; *src; src++) */
	/* 		if (*src == '\n' && src[1] == '\t') */
	/* 			src[1] */
	/* 	ret = mk_bcode(s); */
	/* 	ret->next = markman_parse(src); */
	/* 	return ret; */
	case '`':
		if (src[1] == '`' && src[2] == '`')
			src += strcspn(src, "\n");
		for (s = src; *src; src++)
			if (!strncmp(src, "\n```\n", 5)) {
				/* printf("'%s'\n", src); */
				src[1] = '\0';
				src += 5;
				/* src += strcspn(src, "\n"); */
				break;
			}
		/* printf("'%s'\n", src); */
		ret = mk_bcode(s);
		ret->next = markman_parse(src);
		return ret;
	default:
		src += strspn(src, WS);
		s = ecalloc(strlen(src), sizeof(char));
		for (i = 0; *src; src++, i++)
			if (*src == '\n' && src[1] == '\n') {
				s[i+1] = '\0';
				break;
			} else if (*src == '\n')
				s[i] = ' ';
			else
				s[i] = *src;
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
	case QUOTE:
	case ULIST:
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
		die(0, "%s v%s (c) 2017-2018 Ed van Bruggen", argv0, VERSION);
	default:
		usage(1);
	} ARGEND;

	char buf[BUFSIZ];
	FILE *fp;
	if (*argv) {
		if (!(fp = fopen(*argv, "r")))
			die(1, "%s: %s:", argv[0], *argv);
		while ((fread(buf, 1, sizeof(buf), fp)) > 0) ;
		if (!title) title = *argv;
		markman_disp(markman_parse(buf), title);
	} else {
		while ((fread(buf, 1, sizeof(buf), stdin)) > 0) ;
		markman_disp(markman_parse(buf), "stdin");
	}
	putchar('\n');

	return EXIT_SUCCESS;
}
