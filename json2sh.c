/* This is a JSON to SHell transcoder.
 *
 * This Works is placed under the terms of the Copyright Less License,
 * see file COPYRIGHT.CLL.  USE AT OWN RISK, ABSOLUTELY NO WARRANTY.
 *
 * It has following properties:
 *
 * - Is able to process very big JSON files, as only a single value needs to fit into memory.
 * - Does everything, such that shells can parse things easily.
 * - Is friendly to "read -r"-loops (1 line = 1 value)
 * - Output can be processed by "source" as variables or function calls.
 *
 * Input and output are assumed to be UTF-8.
 *
 * Output format:
 *
 * JSON_<id>=<value>
 *
 * where <value> is a propery shell escaped string,
 * and <id> is the names transformed into some poper escapes:
 *
 * - A-Za-z0-9 are used as is.
 * - _ becomes __, also c is _ in escape mode
 * - Everything else is escaped as _ESCAPE_ with ESCAPE made of:
 * - abdefnrtv are the corresponding control character (where \d is DEL 7F 127)
 * - ghijklmopqsuwxyz are the HEX nibbles in reverse (z=0, g=15, always in pairs)
 * - 0 stands for a . separating the object identifier parts (left away after indexes)
 * - 1-9 followed by other digits including 0 are array indexes.
 * - A-Z switches to a different Unicode planes, A=0 Z=25, AA=26 and so on,
 *   followed by two HEX nibbles (except for plane 0, which can have controls).
 *
 * Examples:
 *
 * '{ "\r": { "\b":1 }}' becomes "JSON__0r0b_=1"
 * '[ true, false, null ]' becomes 'JSON__1_=$JSON_true_' 'JSON__2_=$JSON_false_' 'JSON__3_=$JSON_null_'
 * '[]' becomes 'JSON_=$JSON_empty_'
 * '{}' becomes 'JSON__0_=$JSON_nothing_'
 *
 * Simple strings are output as is.
 * Complex strings are quoted with ''.
 * Very complex strings are quoted with $''.
 *
 * Note that the name is allocated on the stack, so for extreme long names we will still run OOM.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#define	NAME	"json2sh"
#include "VERSION.h"

static int	line;
static int	column;
static struct _buf *PREF, *SEP, *LF;

#if 0
#define	D(...)	debug_printf(__FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
static void
debug_printf(const char *file, int line, const char *fn, const char *s, ...)
{
  va_list	list;

  fprintf(stderr, "[[[%s:%d:%s", file, line, fn);
  va_start(list, s);
  vfprintf(stderr, s, list);
  va_end(list);
  fprintf(stderr, "]]]\n");
  fflush(stderr);
}
static int cc(char c) { return c<0 || !isprint(c) ? '?' : c; }
#else
#define	D(...)	do {} while (0)
#endif
#define	xD(...)	do {} while (0)

/**********************************************************************
 * OUTPUT
 *********************************************************************/

#define	FATAL(X)	do { if (X) OOPS("FATAL ERROR %s:%d:%s: %s", __FILE__, __LINE__, __FUNCTION__, #X); } while (0)

static void
OOPS(const char *s, ...)
{
  int	e=errno;

  va_list	list;

  fflush(stdout);

  fprintf(stderr, NAME ":%d:%d: ", line+1, column+1);
  va_start(list, s);
  vfprintf(stderr, s, list);
  va_end(list);
  fprintf(stderr, ": ");

  errno = e;
  perror(NULL);
  fflush(stderr);

  exit(23);
}

static void
OOPSc(int c, const char *s)
{
  OOPS("%s with character %c (%02x)", s, isprint(c) ? c : ' ', c);
}

static void
outc(char c)
{
  putchar(c);
}

static void
outn(const char *s, size_t len)
{
  fwrite(s, len, 1, stdout);
}

#if 0
static void
out(const char *s)
{
  outn(s, strlen(s));
}
#endif

static void outb(struct _buf *b);

static void
nl(void)
{
  outb(LF);
}

static void
vout(const char *s, va_list list)
{
  vprintf(s, list);
}

static void
outx(int ch)
{
  outc("0123456789abcdef"[ch&15]);
}

static void
oute(int ch)
{
  switch (ch)
    {
    case '\'':
    case '\\':	outc('\\');	outc(ch);	return;
    case '\a':	outc('\\');	outc('a');	return;
    case '\033':outc('\\');	outc('e');	return;
    case '\f':	outc('\\');	outc('f');	return;
    case '\n':	outc('\\');	outc('n');	return;
    case '\r':	outc('\\');	outc('r');	return;
    case '\t':	outc('\\');	outc('t');	return;
    case '\v':	outc('\\');	outc('v');	return;
    }

  if (ch>=' ' && ch<=255 && ch!=127) { outc(ch); return; }

  outc('\\');
  if (ch<256)
    outc('x');
  else
    {
      if (ch<65536)
        outc('u');
      else
        {
          outc('U');
          outx(ch>>28);
          outx(ch>>24);
          outx(ch>>20);
          outx(ch>>16);
        }
      outx(ch>>12);
      outx(ch>>8);
    }
   outx(ch>>4);
   outx(ch);
}

static int
unhex(char c)
{
  if (c>='0' && c<='9')
    return c-'0';
  if (c>='a' && c<='f')
    return c-'a'+10;
  if (c>='A' && c<='F')
    return c-'A'+10;
  return -1;
}

static int
unoct(char c)
{
  if (c>='0' && c<='7')
    return c-'0';
  return -1;
}

/* This is a general unescape.
 * Something in between echo -e, bash printf '%b', C and my ideas
 * Specials:
 * 	\i ignored, so it produces no outpup
 * 	\c end of input, like in `echo -e 'printed\\cignored'
 * REST	\CREST just copy REST uninterpreted.
 * ?	\? if \? is no valid escape, for example \' \" \\
 * c	\? where \? is the C-escape for c
 * DEL	\d
 * ESC	\e or \E
 * NUL	\o or \O
 * c	\0ooo where o is 0-7 and ooo is the octal representation of c
 * c	\Ooo where O is 1-7 and o is 0-7 and Ooo is the octal representation of c
 * c	\xHH where H is 0-9a-f and HH is the hex representation of c
 * No, this does not support unicode yet.
 */
static size_t
unescape(char *dest, const char *s, size_t len, char esc)
{
  size_t	pos, out;
  char		c;
  int		tmp;

  for (out=0, pos=0; pos<len; )
    {
      if ((c = s[pos++])==esc && pos<len)
        switch (c=s[pos++])
          {
          case 'i':	continue;			/* ignore	*/

          case 'C':					/* copy unchanged	*/
            while (pos<len)
              dest[out++] = s[pos++];
          case 'c':	return out;			/* end of string (see 'echo')	*/

          default:	break;				/* dequote anything else	*/

          case 'a':	c='\a'; break;
          case 'b':	c='\b'; break;
          case 'd':	c='\177'; break;		/* DEL, my special	*/
          case 'E':
          case 'e':	c='\033'; break;		/* ESC	*/
          case 'f':	c='\f'; break;
          case 'n':	c='\n'; break;
          case 'r':	c='\r'; break;
          case 't':	c='\t'; break;
          case 'v':	c='\v'; break;
          case 'o':	c=0; break;
          case 'O':	c=0; break;

          case 'x':					/* HEX	*/
            if (pos>=len || (tmp=unhex(s[pos]))<0) break;
            c	= tmp;
            if (++pos>=len || (tmp=unhex(s[pos]))<0) break;
            c	= (c<<4) | tmp;
            pos++;
            break;

          case '0': case '1': case '2': case '3':	/* OCT	*/
          case '4': case '5': case '6': case '7':
            c = unoct(c);
            if (pos>=len || (tmp=unoct(s[pos]))<0) break;
            c	= (c<<3) | tmp;
            if (++pos>=len || c>=(256>>3) || (tmp=unoct(s[pos]))<0) break;
            c	= (c<<3) | tmp;
            if (++pos>=len || c>=(256>>3) || (tmp=unoct(s[pos]))<0) break;
            c	= (c<<3) | tmp;
            pos++;
            break;
          }
      dest[out++]	= c;
    }
  return out;
}


/**********************************************************************
 * MISC
 *********************************************************************/

struct _buf
  {
    const char	*buf;
    size_t	len;
  };

static void
outb(struct _buf *b)
{
  outn(b->buf, b->len);
}

static void *
alloc0(size_t len)
{
  void	*ptr;

  if (!len)
    len	= 1;
  ptr	= calloc(1, len);
  if (!ptr)
    OOPS("out of memory");
  return ptr;
}

static void *
re_alloc(void *buf, size_t len)
{
  void	*ptr;

  if (!len)
    len	= 1;
  ptr	= realloc(buf, len);
  if (!ptr)
    OOPS("out of memory");
  return ptr;
}

struct _buf *
buf(const char *s)
{
  struct _buf	*b;
  char		*tmp;
  size_t	len;

  b		= alloc0(sizeof *b);
  b->len	= len = strlen(s);
  b->buf	= tmp = alloc0(b->len+1);
  memcpy(tmp, s, len);

  /* If buf is surrounded by $'...' do some shell unescape.
   * Ignore leftover bytes, so do not realloc to shrink ..
   */
  if (*s=='\\')
    b->len	= unescape(tmp, s, len, '\\');
  return b;
}


/**********************************************************************
 * INPUT
 *********************************************************************/

static int
get(void)
{
  int	c;

  if ((c=getchar()) == '\n')
    {
      line++;
      column=0;
    }
  else if (c != EOF)
    column++;
  xD("(%d %c)", c, cc(c));
  return c;
}

static int
next(void)
{
  int	c;

  while ((c = get()) != EOF && isspace(c));
  return c;
}

/* Warning: This skips whitespace	*/
static char
peek(void)
{
int	c;

  if ((c=next())!=EOF)
    ungetc(c, stdin);
  xD("(%d %c)", c, cc(c));
  return c;
}

/* Warning: This skips whitespace	*/
static int
have(int want)
{
  int	c;

  if (want==EOF)
    return peek()==EOF;

  if ((c=next())!=EOF)
    {
      if (c == want)
        return 1;
      ungetc(c, stdin);
    }
  return 0;
}

/* Warning: This skips whitespace on the first character	*/
static void
need(const char *c)
{
  int	got;

  xD("(%s)", c);
  if ((got=next()) != (unsigned char)*c)
    OOPS("expected '%s' but got '%c'", c, got);
  while (*++c)
    if ((got=get()) != *c)
      OOPS("missing '%s', got '%c'", c, got);
}

/* Fetch a real character.
 * Bail out on EOF
 */
static int
ch(void)
{
  int	c;

  if ((c = get())==EOF)
    OOPS("unexpected EOF");
  return c;
}

/* Fetch hexadecimal value
 */
static unsigned
hexget(int bits, unsigned val)
{
  unsigned	c;

  c	= ch();
  if (c>='0' && c<='9')
    c	-= '0';
  else if (c>='a' && c<='f')
    c -= 'a'-10;
  else if (c>='A' && c<='F')
    c -= 'A'-10;
  else
    OOPSc(c, "hex digit expected");
  return val | (c<<bits);
}

/* Fetch unicode character.
 *
 * For now we do not decompose the UTF-8 input.
 * This might change in future.
 */
static int
uniget(char end)
{
  int	c;

  if ((c=ch()) < 0)
    OOPS("disallowed control character %d in JSON string", c);

  if (c == end)
    return EOF;

  if (c != '\\')
    return c;

  switch (c=ch())
    {
    case '"':	return c;
    case '\\':	return c;
    case '/':	return c;
    case 'b':	return '\b';
    case 'f':	return '\f';
    case 'n':	return '\n';
    case 'r':	return '\r';
    case 't':	return '\t';
    case 'u':	break;
    default:	OOPSc(c, "unknown escape sequence");
    }

  return hexget(0, hexget(4, hexget(8, hexget(12, 0))));
}


/**********************************************************************
 * Shell variable name (base)
 *********************************************************************/

enum base_type
  {
    B_UNSPEC	= 0,
    B_PREFIX,
    B_ARR,
    B_INDEX,
    B_OBJ,
    B_KEY,
    B_VAL,
  };

typedef struct base *BASE;
struct base
  {
    BASE		next, top;
    enum base_type	type;
    int			done;
    unsigned		esc, cp;
    int			value;
    int			buflen, pos;
    char		*buf;
  };

static BASE	base_freelist;

/* We just give back to the pool.
 * No cleanups, as we can reuse the buffers later.
 */
static BASE
base_free(BASE b)
{
  BASE	tmp	= b->next;

  b->next	= base_freelist;
  b->type	= B_UNSPEC;

  base_freelist	= b;
  return tmp;
}

/* Append some unicode character to our base.
 */
static void
base_put(BASE b, int c)
{
  if (b->pos >= b->buflen)
    {
      b->buflen	+= BUFSIZ;
      b->buf	=  re_alloc(b->buf, b->buflen);
    }
  b->buf[b->pos++]	= c;
  if (b->type != B_VAL)
    outc(c);
}

static void
base_esc_end(BASE b)
{
  D("(%d)", b->esc);
  if (b->esc)
    base_put(b, '_');
  b->esc	= 0;
  b->cp		= 0;
}

/* actually, this is a hack	*/
static BASE
base_child(BASE p, BASE b)
{
  FATAL(b->type==B_UNSPEC);
  if (p)
    {
      FATAL(b==p);
      FATAL(p->type==B_UNSPEC);
      FATAL(p->next && p->next!=b);
      p->next	= b;
      if (b->done)
        p->done	= 1;

      /* finish building variable name when value node follows	*/
      if (b->type == B_VAL)
        base_esc_end(p);

      /* copy codepage and esc mode from parent	*/
      b->esc	= p->esc;
      b->cp	= p->cp;
    }
  FATAL(b->next);
  return b;
}

static BASE
base_new(BASE p, enum base_type type)
{
  BASE	b;

  FATAL(p && p->type==B_UNSPEC);

  if (!base_freelist)
    base_freelist	= alloc0(sizeof *base_freelist);

  b		= base_freelist;
  base_freelist	= b->next;

  FATAL(b->type!=B_UNSPEC);

  b->next	= 0;
  b->top	= p ? p->top : b;
  b->type	= type;
  b->done	= 0;
  b->pos	= 0;

  return base_child(p, b);
}

/* Cut the remaining base->next pointers.
 * This is because we are lazy.
 * We create base(parent), but we do not free it.
 *
 * Due to recursion, we know the sub-thing is no more needed,
 * so we can give it back to the pool.
 */
static void
base_cut(BASE p)
{
  BASE	b;

  if (p)
    {
      for (b=p->next; b; b=base_free(b))
        if (b->done)
          p->done	= 1;
      p->next	= 0;
    }
}

static int
base_done(BASE b)
{
  base_cut(b);
  return b->done;
}

/* Print out (repeat) the SHell variable name up to here
 */
static void
base_print(BASE b)
{
  D("(%p %d)", b, b->type);
  nl();
  for (; b; b=b->next)
    {
      outn(b->buf, b->pos);
      b->done	= 0;
    }
}

/* Setup for a new value to print out
 */
static BASE
base(BASE p, enum base_type type)
{
  D("(%p t=%d)", p, type);
  base_cut(p);

  D(" x1");
  if (p && p->done)
    base_print(p->top);

  D(" x2");
  return base_new(p, type);
}

static void
base_fin(BASE b)
{
  base_esc_end(b);
  if (!b->done)
    outb(SEP);
  b->done	= 1;
}

static void
base_out(BASE b, const char *s, ...)
{
  va_list	list;

  base_fin(b);

  va_start(list, s);
  vout(s, list);
  va_end(list);
}

/* Send character, perhaps switching in esc mode:
 * 0: plain characters (0-9 A-Z a-z)
 * 1: indexes (1-999999999999999999999)
 * 2: object separator _0_
 * 3: all other escapes
 */
static void
base_esc(BASE b, int c, int esc)
{
  if (b->esc != esc && (b->esc & esc)==0)
    {
      base_esc_end(b);
      if (esc)
        base_put(b, '_');
    }
  b->esc	= esc;
  base_put(b, c);
}

/* literal output (variable name)
 */
static void
base_set(BASE b, struct _buf *buf)
{
  int i;

  for (i=0; i<buf->len; i++)
    base_esc(b, buf->buf[i], 0);
}

static BASE
base_index(BASE p, int index)
{
  BASE	b = base(p, B_INDEX);
  char	buf[200], *ptr;

  base_esc_end(b);
  snprintf(buf, sizeof buf, "%d", index);
  for (ptr=buf; *ptr; )
    base_esc(b, *ptr++, 1);
  return b;
}

static void
base_hex(BASE b, int hex)
{
  base_esc(b, "zyxwusqpomlkjihg"[hex&0xf], 3);
}

static void
base_cp26(BASE b, unsigned cp)
{
  if (cp>25)
    base_cp26(b, cp/26);
  base_esc(b, "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[cp%26], 3);
}

static void
base_cp(BASE b, unsigned cp)
{
  if (b->cp==cp)
    return;

  b->cp	= cp;
  base_cp26(b, cp);
}

static int
simple_value(int ch)
{
  return (ch>='0' && ch<='9') || (ch>='A' && ch<='Z') || (ch>='a' && ch<='z');
}

static void
base_escape(BASE b, int ch)
{
  switch (ch)
    {
    case EOF:	return;
    case '_':
      /* perhaps delay a bit, to see if we enter or leave ->esc	*/
      if (b->esc)
        base_esc(b, 'c', 3);
      else
        {
          base_esc(b, ch, 0);
          base_esc(b, ch, 0);
        }
      return;

    case '\a':	base_esc(b, 'a', 3);	return;
    case '\b':	base_esc(b, 'b', 3);	return;
    case '\177':base_esc(b, 'd', 3);	return;
    case '\033':base_esc(b, 'e', 3);	return;
    case '\f':	base_esc(b, 'f', 3);	return;
    case '\n':	base_esc(b, 'n', 3);	return;
    case '\r':	base_esc(b, 'r', 3);	return;
    case '\t':	base_esc(b, 't', 3);	return;
    case '\v':	base_esc(b, 'v', 3);	return;
    }

  if (simple_value(ch))
    {
      base_esc(b, ch, 0);
      return;
    }

  base_cp(b, ch>>8);
  base_hex(b, ch>>4);
  base_hex(b, ch);
}

static void
base_add(BASE b, int ch)
{
  if (ch == EOF)
    switch (b->value)
      {
      case 0: outn(b->buf, b->pos); return;
      case 1: outc('\''); outn(b->buf, b->pos);
      default: outc('\''); return;
      }
  if (b->value<2 && b->pos < 255)
    if ((b->value==0 && simple_value(ch)) || (b->value=1, ch>=32 && ch<=255 && ch!='\'' && ch!=127))
      {
        base_put(b, ch);
        return;
      }
  if (b->value < 3)
    {
      int	i;

      outn("$'", 2);
      for (i=0; i<b->pos; i++)
        oute((unsigned char)b->buf[i]);
    }
  b->value	= 3;
  oute(ch);
}


static int
base_if(BASE b, const char *chars)
{
  int	c;

  c	= ch();
  if (!strchr(chars, c))
    {
      ungetc(c, stdin);
      return 0;
    }
  base_fin(b);
  base_add(b, c);
  return 1;
}

static int
base_digit(BASE b)
{
  return base_if(b, "0123456789");
}

static void
base_digits(BASE b)
{
  if (!base_digit(b))
    OOPS("number expected");
  while (base_digit(b));
}


/**********************************************************************
 * JSON helpers
 *********************************************************************/


/* if b!=NULL then assemble a string suitable for shell variable,
 * else assemble a string suitable for shell variable content.
 */
static BASE
get_string(BASE p)
{
  BASE	b = base(p, B_VAL);
  int	c;

  base_fin(b);
  D("");
  need("\"");
  while ((c=uniget('"'))!=EOF)
    base_add(b, c);
  base_add(b, EOF);
  D(" ret");

  return b;
}

static BASE
get_key(BASE p)
{
  BASE	b = base(p, B_KEY);
  int	c;

  need("\"");
  while ((c=uniget('"'))!=EOF)
    base_escape(b, c);
  base_escape(b, EOF);

  return b;
}

/**********************************************************************
 * JSON datatypes
 *********************************************************************/

void j_value(BASE b);

static void
j_string(BASE b)
{
  D("");
  FATAL(b->next);
  get_string(b);
  D(" ret");
}

static void
j_const(BASE b, const char *var)
{
  D("var=%s", var);
  need(var);
  base_out(b, "$JSON_%s_", var);
}

static void
j_object(BASE p)
{
  BASE	b	= base(p, B_OBJ);

  D("(%d)", b->done);
  if (p->type!=B_INDEX)
    base_esc(b, '0', 2);
  D("(%d)", b->done);

  need("{");
  while (!have('}'))
    {
      BASE	t;

      if (base_done(b))
        need(",");
      D(" here2");
      t	= get_key(b);
      need(":");
      FATAL(t->next);
      D(" here3 %d", t->done);
      j_value(t);
      D(" here1");
    }
  if (!base_done(b))
    base_out(b, "$JSON_nothing_");
}

static void
j_array(BASE p)
{
  BASE	b	= base(p, B_ARR);
  int		index=0;

  D("()");
  need("[");
  while (!have(']'))
    {
      BASE	t;

      if (base_done(b))
        need(",");
      t	= base_index(b, ++index);
      j_value(t);
    }
  if (!base_done(b))
    base_out(b, "$JSON_empty_");
  D(" ret");
}

static void
j_number(BASE p)
{
  BASE	b = base(p, B_VAL);
  D("()");
  base_if(b, "-");

  if (!base_if(b, "0"))
    base_digits(b);	/* we know it is not 0	*/

  if (base_if(b, "."))
    base_digits(b);

  if (base_if(b, "eE"))
    {
      base_if(b, "+-");
      base_digits(b);
    }
  base_fin(b);
  base_add(b, EOF);
  D(" ret");
}

void
j_value(BASE b)
{
  D("()");
  switch (peek())
    {
      case EOF:	OOPS("unexpected EOF");
      case '{':	j_object(b);			break;
      case '[':	j_array(b);			break;
      case '"':	j_string(b);			break;
      case 't':	j_const(b,	"true");	break;
      case 'f':	j_const(b,	"false");	break;
      case 'n':	j_const(b,	"null");	break;
      default:	j_number(b);			break;
    }
  D(" ret");
}

/**********************************************************************
 * main
 *********************************************************************/

int
main(int argc, char **argv)
{
  BASE	b;

  if (argc>4 || (argc>1 && argv[1][0]=='-'))
    {
      fprintf(stderr, "Usage: %s [PREFIX [SEP [LF]]]\n"
              "\t\tVersion " VERSION " from " GITDATE " (" GITCOMMIT ")\n"
              "\tConvert any JSON into lines readable by shell.\n"
              "\tdefault: PREFIX='JSON_' SEP='=' LF='\\n'\n"
              "\tPREFIX/SEP/LF are de-escaped if they start with '\\'.\n"
              "\t\t\\i to ignore the initial '\\'.\n"
              "\t\t\\c to ignore the rest of the string.\n"
              "\t\t\\C to copy the rest of the string as-is.\n"
              "\tExamples:\n"
              "\t\tUse $ARG from env as-is: '\\C'\"$ARG\"\n"
              "\t\tWrite ARGs like '-\\r\\n' as '\\i''-\\r\\n'\n"
              "\t\tjson2sh <<< '[ true, false, null, [], {} ]'\n"
              , NAME);
      return 42;
    }

  PREF	= buf(argc>1 ? argv[1] : "JSON_");
  SEP	= buf(argc>2 ? argv[2] : "=");
  LF	= buf(argc>3 ? argv[3] : "\n");

  b	= base_new(NULL, B_PREFIX);
  base_set(b, PREF);
  j_value(b);
  if (peek()!=EOF)
    OOPS("end of input expected");
  if (base_done(b))
    nl();

  return 0;
}

