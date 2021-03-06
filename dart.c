/* Copyright (C) 2021 Lars Brinkhoff <lars@nocrew.org>

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

#include <time.h>
#include <utime.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include "dis.h"

#define DART  0444162640000LL
#define HEAD  0125045414412LL
#define TAIL  0126441515412LL

#define MAX 10240
static word_t block[MAX];
static int extract = 0;
static int verbose = 0;

static FILE *list;
static FILE *info;

static FILE *output;
static word_t checksum;
static char file_path[100];
static struct timeval timestamp[2];
static int dart;
static int iover;

static void
compute_date (word_t date, int *year, int *month, int *day)
{
  *day = date % 31;
  *month = (date / 31) % 12;
  *year = (date / 31 / 12) + 1964;
}

static void
unix_time (struct timeval *tv, word_t date, word_t minutes)
{
  struct tm tm;
  compute_date (date, &tm.tm_year, &tm.tm_mon, &tm.tm_mday);
  tm.tm_sec = 0;
  tm.tm_min = minutes % 60;
  tm.tm_hour = minutes / 60;
  tm.tm_mday++;
  tm.tm_year -= 1900;
  tm.tm_isdst = 0;
  tv->tv_sec = mktime (&tm);
  tv->tv_usec = 0;
}

static void
print_timestamp (FILE *f, word_t date, word_t minutes)
{
  int year, month, day;
  compute_date (date, &year, &month, &day);
  fprintf (f, "%4d-%02d-%02d %02lld:%02lld",
	   year, month + 1, day + 1,
	   minutes / 60, minutes % 60);
}

static int
right (word_t word)
{
  return word & 0777777LL;
}

static int
left (word_t word)
{
  return right (word >> 18);
}

static void
get_block (FILE *f, word_t *buffer, int words)
{
  int i;

  if (words > MAX)
    {
      fprintf (stderr, "EXPECTED SMALLER RECORD LENGTH %d\n", words);
      exit (1);
    }

  checksum = 0;
  for (i = 0; i < words; i++)
    {
      buffer[i] = get_word (f);
      //fprintf (stderr, "[%04o: %012llo]\n", i, buffer[i]);
      if (buffer[i] == -1)
	{
	  fprintf (stderr, "Physical end of tape.\n");
	  exit (1);
	}
      if (buffer[i] & (START_FILE|START_RECORD))
	{
	  fprintf (stderr, "Record too short.\n");
	  exit (1);
	}
      checksum ^= buffer[i];
    }
}

static void
close_file (void)
{
  fprintf (stdout, "CLOSE %s\n", file_path);
  fclose (output);
  output = NULL;
  utimes (file_path, timestamp);
}

static void
open_file (char *directory, char *name, char *ext)
{
  weenixname (directory);
  fprintf (info, "DIRECTORY: %s\n", directory);
  if (mkdir (directory, 0777) == -1 && errno != EEXIST)
    fprintf (stderr, "Error creating output directory %s: %s\n",
	     directory, strerror (errno));

  weenixname (name);
  weenixname (ext);
  strcpy (file_path, directory);
  strcat (file_path, "/");
  strcat (file_path, name);
  if (*ext != 0)
    {
      strcat (file_path, ".");
      strcat (file_path, ext);
    }

  fprintf (info, "FILE: %s\n", file_path);
  output = fopen (file_path, "wb");
  if (output == NULL)
    fprintf (stderr, "Error opening output file %s: %s\n",
	     file_path, strerror (errno));
}

static void
write_block (word_t *data, int size)
{
  int i;
  for (i = 0; i < size; i++)
    write_word (output, *data++);
}

static word_t
read_start (FILE *f, int length)
{
  word_t word, date, minutes;
  char string[8];
  char project[4], programmer[4];
  char directory[7], name[7], ext[7];

  get_block (f, block + 1, length);

  sixbit_to_ascii (block[1], string);
  string[6] = ' ';
  string[7] = ' ';
  *strchr (string, ' ') = ':';
  fprintf (list, "   %s", string);
  *strchr (string, ':') = ' ';
  sixbit_to_ascii (block[2], name);
  fprintf (list, "%s", name);
  sixbit_to_ascii (block[3], ext);
  ext[3] = 0;
  fprintf (list, ".%s", ext);

  sixbit_to_ascii (block[5], string);
  strncpy (project, string, 3);
  project[3] = 0;
  strncpy (programmer, string + 3, 3);
  programmer[3] = 0;
  sprintf (directory, "%s,%s", project, programmer);
  fprintf (list, "[%s]   ", directory);

  date = block[4] & 07777;
  if (dart >= 5)
    date |= block[3] & 070000;
  minutes = (block[4] >> 12) & 03777;
  unix_time (&timestamp[0], date, minutes);
  unix_time (&timestamp[1], date, minutes);
  print_timestamp (list, date, minutes);
  //fprintf (list, " [%ld]", ftell (f));
  fputc ('\n', list);

  if (extract)
    {
      open_file (directory, name, ext);
      write_block (&block[22], length - 022);
    }

  word = get_word (f);
  if (checksum != word)
    fprintf (stderr, "Bad checksum: %012llo != %012llo\n", word, checksum);
  else
    fprintf (info, "Good checksum: %012llo\n", checksum);

  word = get_word (f);
  if (left (word) != 0 && extract)
    close_file ();

  return word;
}

static word_t
read_data (FILE *f, int length)
{
  word_t word;

  get_block (f, block, length);
  if (extract)
    write_block (block, length);

  word = get_word (f);
  if (checksum != word)
    fprintf (stderr, "Bad checksum: %012llo != %012llo\n", word, checksum);
  else
    fprintf (info, "Good checksum: %012llo\n", checksum);

  word = get_word (f);
  if (left (word) != 0 && extract)
    close_file ();
  return word;
}

static void
read_header (FILE *f, word_t word)
{
  int length;
  char string[7];
  char project[4], programmer[4];
  word_t date, minutes;

  length = right (word);
  get_block (f, block + 1, length);

  if (block[2] != HEAD && block[2] != TAIL)
    fprintf (stderr, "EXPECTED HEAD OR TAIL\n");

  dart = left (word);
  fprintf (list, "DART VERSION %-2d TAPE %s\n",
	   dart, block[2] == HEAD ? "HEADER" : "TRAILER");

  if (block[1] != DART)
    fprintf (stderr, "EXPECTED DART MAGIC\n");

  sixbit_to_ascii (block[4], string);
  strncpy (project, string, 3);
  project[3] = 0;
  strncpy (programmer, string + 3, 3);
  programmer[3] = 0;
  fprintf (list, "RECORDED ");
  date = block[3] & 07777;
  date |= (block[3] >> 21) & 070000;
  minutes = (block[3] >> 12) & 03777;
  print_timestamp (list, date, minutes);
  fprintf (list, ",  BY [%s,%s] %s CLASS",
           project, programmer,
           left (block[5]) ? "SYSTEM" : "USER");
  if (left (block[5]))
    fprintf (list, " TAPE #%d", right (block[5]));
  fputc ('\n', list);
  if (block[2] == TAIL)
    fputc ('\n', list);
}

static word_t
read_gap (FILE *f)
{
  word_t word = get_word (f);
  int i;

  word--;
  for (i = 0; i < word; i++)
    {
      if (get_word (f) == -1)
	{
	  fprintf (stderr, "Physical end of tape.\n");
	  exit (1);
	}
    }
  
  fprintf (list, "   GAP\n");
  return get_word (f);
}

static word_t
read_record (FILE *f, word_t word)
{
  int length = right (word);
  if (word == -1)
    {
      fprintf (stderr, "END OF TAPE (NO TRAILER)\n");
      exit (0);
    }
  switch (left (word))
    {
    case 0:
      fprintf (info, "RECORD: DATA\n");
      return read_data (f, length);
    case 0777775:
    case 0777776:
    case 0777777:
      iover = 01000000 - left (word);
      fprintf (info, "RECORD: FILE (IOVER%d)\n", iover);
      return read_start (f, length);
    case 0777767:
      fprintf (info, "RECORD: GAP\n");
      return read_gap (f);
    default:
      fprintf (info, "RECORD: HEAD\n");
      if (left (word) <10)
	{
	  read_header (f, word);
	  return get_word (f);
	}
      else
        {
          fprintf (stderr, "EXPECTED RECORD TYPE got %o\n", left (word));
          exit (1);
        }
    }
}

static void
process_tape (FILE *f)
{
  word_t word = get_word (f);
  if (word == -1)
    {
      fprintf (stderr, "EMPTY TAPE\n");
      exit (1);
    }
  for (;;)
    word = read_record (f, word);
}

static void
usage (const char *x)
{
  fprintf (stderr, "Usage: %s -t|-x [-v789] [-Wformat] [-f file]\n", x);
  usage_word_format ();
  exit (1);
}

int
main (int argc, char **argv)
{
  FILE *f = NULL;
  int opt;

  input_word_format = &tape7_word_format;
  output_word_format = &aa_word_format;

  if (argc == 1)
    usage (argv[0]);

  while ((opt = getopt (argc, argv, "tvx789f:W:")) != -1)
    {
      switch (opt)
	{
	case 'f':
	  if (f != NULL)
	    {
	      fprintf (stderr, "Just one -f allowed.\n");
	      exit (1);
	    }
	  f = fopen (optarg, "rb");
	  if (f == NULL)
	    {
	      fprintf (stderr, "Error opening input %s: %s\n",
		       optarg, strerror (errno));
	      exit (1);
	    }
	  break;
	case 't':
	  if (extract)
	    {
	      fprintf (stderr, "Just one of -t or -x allowed.\n");
	      exit (1);
	    }
	  verbose++;
	  break;
	case 'v':
	  verbose++;
	  break;
	case 'x':
	  if (extract)
	    {
	      fprintf (stderr, "Just one of -t or -x allowed.\n");
	      exit (1);
	    }
	  extract = 1;
	  break;
	case '7':
	  input_word_format = &tape7_word_format;
	  break;
	case '8':
	  input_word_format = &data8_word_format;
	  break;
	case '9':
	  input_word_format = &tape_word_format;
	  break;
	case 'W':
	  if (parse_output_word_format (optarg))
	    usage (argv[0]);
	  break;
	default:
	  usage (argv[0]);
	}
    }

  if (f == NULL)
    f = stdin;

  list = info = stdout;
  if (verbose == 0)
    list = info = fopen ("/dev/null", "w");
  else if (verbose == 1)
    info = fopen ("/dev/null", "w");

  process_tape (f);

  return 0;
}
