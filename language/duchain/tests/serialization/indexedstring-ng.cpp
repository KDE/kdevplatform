/*
 * This file is part of KDevelop
 * Copyright 2012 Milian Wolff <mail@milianw.de>
 * Copyright 2008 David Nolden <david.nolden.kdevelop@art-master.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License or (at your option) version 3 or any later version
 * accepted by the membership of KDE e.V. (or its successor approved
 * by the membership of KDE e.V.), which shall act as a proxy
 * defined in Section 14 of version 3 of the license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "indexedstring-ng.h"

#include <language/duchain/repositories/stringrepository.h>
#include <language/duchain/referencecounting.h>

#include <KUrl>

namespace KDevelop {
namespace {
struct IndexedStringNGData
{
  int length;
  uint refCount;
  inline uint itemSize() const
  {
    return sizeof(IndexedStringNGData) + length * sizeof(QChar);
  }
  inline uint hash() const
  {
    return qHash(QString::fromRawData(reinterpret_cast<const QChar*>(this+1), length));
  }
  inline QString string() const
  {
    return QString(reinterpret_cast<const QChar*>(this+1), length);
  }
};

inline void increase(uint& val)
{
  ++val;
}

inline void decrease(uint& val)
{
  --val;
}

struct IndexedStringNGRepositoryItemRequest
{
  IndexedStringNGRepositoryItemRequest(const QString& string)
  : m_hash(qHash(string))
  , m_str(string)
  {
  }

  enum {
    AverageSize = 10 //This should be the approximate average size of an Item
  };

  typedef uint HashType;

  //Should return the hash-value associated with this request(For example the hash of a string)
  HashType hash() const
  {
    return m_hash;
  }

  //Should return the size of an item created with createItem
  uint itemSize() const
  {
    return sizeof(IndexedStringNGData) + m_str.length() * sizeof(QChar);
  }
  //Should create an item where the information of the requested item is permanently stored. The pointer
  //@param item equals an allocated range with the size of itemSize().
  void createItem(IndexedStringNGData* item) const
  {
    item->length = m_str.length();
    item->refCount = 0;
    memcpy(item+1, m_str.constData(), m_str.length() * sizeof(QChar));
  }

  static void destroy(IndexedStringNGData* item, KDevelop::AbstractItemRepository&)
  {
    Q_UNUSED(item);
    //Nothing to do here (The object is not intelligent)
  }

  static bool persistent(const IndexedStringNGData* item)
  {
    return (bool)item->refCount;
  }

  //Should return whether the here requested item equals the given item
  bool equals(const IndexedStringNGData* item) const
  {
    return item->length == m_str.length() && (memcmp(++item, m_str.constData(), m_str.length() * sizeof(QChar)) == 0);
  }
  uint m_hash;
  QString m_str;
};

typedef ItemRepository<IndexedStringNGData, IndexedStringNGRepositoryItemRequest, false, false> IndexedStringNGRepository;

RepositoryManager<IndexedStringNGRepository>& getGlobalIndexedStringNGRepository()
{
  static RepositoryManager<IndexedStringNGRepository> globalIndexedStringNGRepository("String Index NG",
                                                                                      new QMutex(QMutex::NonRecursive));
  return globalIndexedStringNGRepository;
}

/**
 * Emplace the unicode value of @p c in the upper four bytes of the index
 */
inline uint charToIndex(QChar c)
{
  return 0xffff0000 | c.unicode();
}

/**
 * Read the unicode value of @p index emplaced in the upper four bytes.
 */
inline QChar indexToChar(uint index)
{
  return QChar((ushort)index & 0xff);
}

/**
 * Check whether the index is a char
 */
inline bool isChar(uint index)
{
  return (index & 0xffff0000) == 0xffff0000;
}

/**
 * Check whether the index is non-trivial
 */
inline uint isNonTrivial(uint index)
{
  return index && !isChar(index);
}

}

IndexedStringNG::IndexedStringNG()
: m_index(0)
{
}


IndexedStringNG::IndexedStringNG(QChar c)
: m_index(charToIndex(c))
{
}

IndexedStringNG::IndexedStringNG(const QString& string)
{
  if (string.isEmpty()) {
    m_index = 0;
  } else if (string.length() == 1) {
    m_index = charToIndex(string.at(0));
  } else {
    QMutexLocker lock(getGlobalIndexedStringNGRepository()->mutex());
    m_index = getGlobalIndexedStringNGRepository()->index(IndexedStringNGRepositoryItemRequest(string));

    //TODO: this could be merged with the above if the ItemRepository API got a
    //      Action argument for the index member function.
    if (shouldDoDUChainReferenceCounting(this)) {
      increase(getGlobalIndexedStringNGRepository()->dynamicItemFromIndexSimple(m_index)->refCount);
    }
  }
}

IndexedStringNG::~IndexedStringNG()
{
  if (isNonTrivial(m_index)) {
    if (shouldDoDUChainReferenceCounting(this)) {
      QMutexLocker lock(getGlobalIndexedStringNGRepository()->mutex());
      decrease(getGlobalIndexedStringNGRepository()->dynamicItemFromIndexSimple(m_index)->refCount);
    }
  }
}

IndexedStringNG::IndexedStringNG(const IndexedStringNG& rhs)
: m_index(rhs.m_index)
{
  if (isNonTrivial(m_index)) {
    if (shouldDoDUChainReferenceCounting(this)) {
      QMutexLocker lock(getGlobalIndexedStringNGRepository()->mutex());
      increase(getGlobalIndexedStringNGRepository()->dynamicItemFromIndexSimple(m_index)->refCount);
    }
  }
}

IndexedStringNG& IndexedStringNG::operator=(const IndexedStringNG& rhs)
{
  if (m_index == rhs.m_index) {
    return *this;
  }

  // decrease refcount of current/old index
  if (isNonTrivial(m_index)) {
    if (shouldDoDUChainReferenceCounting(this)) {
      QMutexLocker lock(getGlobalIndexedStringNGRepository()->mutex());
      decrease(getGlobalIndexedStringNGRepository()->dynamicItemFromIndexSimple(m_index)->refCount);
    }
  }

  m_index = rhs.m_index;

  // increase refcount for new index
  if (isNonTrivial(m_index)) {
    if(shouldDoDUChainReferenceCounting(this)) {
      QMutexLocker lock(getGlobalIndexedStringNGRepository()->mutex());
      increase(getGlobalIndexedStringNGRepository()->dynamicItemFromIndexSimple(m_index)->refCount);
    }
  }

  return *this;
}

QString IndexedStringNG::toString() const
{
  if (!m_index) {
    return QString();
  } else if(isChar(m_index)) {
    return QString(indexToChar(m_index));
  } else {
    QMutexLocker lock(getGlobalIndexedStringNGRepository()->mutex());
    return getGlobalIndexedStringNGRepository()->itemFromIndex(m_index)->string();
  }
}

int IndexedStringNG::length() const
{
  return lengthFromIndex(m_index);
}

int IndexedStringNG::lengthFromIndex(uint index)
{
  if(!index) {
    return 0;
  } else if (isChar(index)) {
    return 1;
  } else {
    QMutexLocker lock(getGlobalIndexedStringNGRepository()->mutex());
    return getGlobalIndexedStringNGRepository()->itemFromIndex(index)->length;
  }
}

}

QDebug operator<<(QDebug s, const KDevelop::IndexedStringNG& string)
{
  s.nospace() << string.toString();
  return s.space();
}
