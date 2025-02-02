/* Copyright (C) 2018 Lars Brinkhoff <lars@nocrew.org>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "dis.h"

int absolute = 0;
int image = 0;
unsigned char memory[65536];
int start = 65536, end = -1;
void (*out_fn) (FILE *, FILE *, int, int, int);
void (*symtab_fn) (FILE *, FILE *);

static void
usage (const char *x)
{
  fprintf (stderr, "Usage: %s -I|A [-W<format>]\n", x);
  exit (1);
}

static int
pdp11_in (FILE *f)
{
  word_t w1, w2;

  w1 = get_word (f);
  w2 = get_word (f);
  if (w1 == -1 || w2 == -1)
    exit (0);

  return (w1 & 0377) | ((w2 & 0377) << 8);
}

static void
pdp11_out (FILE *f, int word)
{
  fputc (word & 0377, f);
  fputc ((word >> 8) & 0377, f);
}

static void absolute_out (FILE *in, FILE *out, int first,
			  int address, int count)
{
  int i, c;

  pdp11_out (out, first);
  pdp11_out (out, count);
  pdp11_out (out, address);

  if (count == 6)
    symtab_fn (in, stderr);

  for (i = 6; i < count; i++)
    {
      c = get_word (in);
      fputc (c & 0377, out);
    }

  c = get_word (in);
  fputc (c & 0377, out);
}

static void image_out (FILE *in, FILE *out, int first, int address, int count)
{
  int i, c;
  
  count -= 6;

  if (count == 0)
    {
      fprintf (stderr, "Image start: %06o\n", start);
      for (i = start; i < end; i++)
	fputc (memory[i], out);
      symtab_fn (in, stderr);
    }

  if (address < start)
    start = address;
  if (address + count > end)
    end = address + count;

  for (i = 0; i < count; i++)
    {
      c = get_word (in);
      memory[address + i] = c;
    }

  c = get_word (in);
}

static void
block (FILE *in, FILE *out)
{
  int first, count, address;

  for (first = 0; ((first = pdp11_in (in)) & 0377) != 1; )
    ;
  
  count = pdp11_in (in);
  address = pdp11_in (in);

  if (count == 6 && (address & 1) == 0)
    fprintf (stderr, "Program start: %06o\n", address);

  out_fn (in, out, first, address, count);
}

static void
symtab (FILE *in, FILE *out)
{
  char name[7];
  word_t w;

  /* Checksum of previous block. */
  w = get_word (in);

  /* Start of symbol table. */
  w = get_word (in);
  if (w != 2)
    {
      fprintf (stderr, "Bad symbol table.\n");
      exit (1);
    }

  for (;;)
    {
      w = get_word (in);
      if (w == -1 || w == 0)
	exit (0);

      sixbit_to_ascii (w, name);
      w = get_word (in);
      fprintf (out, "%06llo %6s (", w & 0777777, name);
      w >>= 18;
      if (w & 020000)
	fprintf (out, "half killed ");
      if (w & 001000)
	fprintf (out, "label");
      else if (w & 004000)
	fprintf (out, "register");
      else 
	fprintf (out, "symbol");
      fprintf (out, ")\n");
    }
}

static void
no_symtab (FILE *in, FILE *out)
{
  exit (0);
}

int
main (int argc, char **argv)
{
  int opt;

  input_word_format = &its_word_format;
  symtab_fn = no_symtab;

  while ((opt = getopt (argc, argv, "AISW:")) != -1)
    {
      switch (opt)
	{
	case 'W':
	  if (parse_input_word_format (optarg))
	    usage (argv[0]);
	  break;
	case 'A':
	  absolute = 1;
	  break;
	case 'I':
	  image = 1;
	  memset (memory, 0, sizeof memory);
	  break;
	case 'S':
	  symtab_fn = symtab;
	  break;
	default:
	  usage (argv[0]);
	}
    }

  if (optind != argc)
    usage ("optind");

  if ((absolute ^ image) == 0)
    {
      fprintf (stderr, "Must specify one of -A or -I for output format.\n");
      exit (1);
    }

  if (absolute)
    out_fn = absolute_out;
  if (image)
    out_fn = image_out;

  for (;;)
    block (stdin, stdout);

  return 0;
}
