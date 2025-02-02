/* Copyright (C) 2013 Lars Brinkhoff <lars@nocrew.org>
   Copyright (C) 2020 Adam Sampson <ats@offog.org>

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
#include <string.h>

#include "dis.h"

struct file_format *input_file_format = NULL;

static struct file_format *file_formats[] = {
  &dmp_file_format,
  &mdl_file_format,
  &pdump_file_format,
  &raw_file_format,
  &sblk_file_format,
  &shr_file_format,
  NULL
};

void
usage_file_format (void)
{
  int i;

  fprintf (stderr, "Valid file formats are:");
  for (i = 0; file_formats[i] != NULL; i++)
    fprintf (stderr, " %s", file_formats[i]->name);
  fprintf (stderr, "\n");
}

int
parse_input_file_format (const char *string)
{
  int i;

  for (i = 0; file_formats[i] != NULL; i++)
    if (strcmp (string, file_formats[i]->name) == 0)
      {
        input_file_format = file_formats[i];
        return 0;
      }

  return -1;
}

void
guess_input_file_format (FILE *file)
{
  word_t word = get_word (file);
  rewind_word (file);

  if ((word >> 18) == 01776)
    input_file_format = &shr_file_format;
  else if (word == 0)
    input_file_format = &pdump_file_format;
  else
    input_file_format = &sblk_file_format;
}
