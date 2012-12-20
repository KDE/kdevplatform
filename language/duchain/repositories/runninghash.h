/*
   Copyright 2008 David Nolden <david.nolden.kdevelop@art-master.de>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#ifndef RUNNINGHASH_H
#define RUNNINGHASH_H

namespace KDevelop {

/**
 * Use this to construct a hash-value on-the-fly
 *
 * To read it, just use the hash member, and when a new string is started, call @c clear().
 *
 * This needs very fast performance(per character operation), so it must stay inlined.
 */
struct RunningHash
{
  enum {
    HashInitialValue = 5381
  };

  inline RunningHash()
  : hash(HashInitialValue)
  {
  }

  inline void append(const char c)
  {
    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
  }

  inline void clear()
  {
    hash = HashInitialValue;
  }

  unsigned int hash;

  inline static unsigned int hashString(const char* string, unsigned short length)
  {
    if (!length) {
      return 0;
    }

    RunningHash hasher;
    for(unsigned short i = 0; i < length; ++i) {
      hasher.append(string[i]);
    }
    return hasher.hash;
  }
};

}

#endif // RUNNINGHASH_H
