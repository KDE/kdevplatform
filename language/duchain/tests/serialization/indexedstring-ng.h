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

#ifndef INDEXED_STRING_NG_H
#define INDEXED_STRING_NG_H

//krazy:excludeall=dpointer,inline

#include <QtCore/QMetaType>

#include <KUrl>

#include <language/duchain/referencecounting.h>

namespace KDevelop {

/**
 * This string does "disk reference-counting", which means that reference-counts are maintainted,
 * but only when the string is in a disk-stored location. The file referencecounting.h is used
 * to manage this condition.
 *
 * Whenever reference-counting is enabled for a range that contains the IndexedStringNG, it will
 * manipulate the reference-counts.
 *
 * The duchain storage mechanisms automatically are about correctly managing that condition,
 * so you don't need to care, and can just use this class in every duchain data type without
 * restrictions.
 *
 * @warning Do not use IndexedStringNG after QCoreApplication::aboutToQuit() has been emitted,
 * items that are not disk-referenced will be invalid at that point.
 *
 * @note Empty strings have an index of zero.
 *
 * @note Strings of length one are not put into the repository, but are encoded directly within
 * the index: They are encoded like @c 0xffff00bb where @c bb is the byte of the character.
 */
class KDEVPLATFORMLANGUAGE_EXPORT IndexedStringNG {
public:
  /**
   * Create an empty IndexedStringNG.
   */
  IndexedStringNG();

  /**
   * Index the given char @p c.
   */
  explicit IndexedStringNG(QChar c);

  /**
   * Index the given @p string.
   */
  explicit IndexedStringNG(const QString& string);

  /**
   * Copy constructor.
   */
  IndexedStringNG(const IndexedStringNG& other);

  /**
   * Assignment operator.
   */
  IndexedStringNG& operator=(const IndexedStringNG&);

  /**
   * Destructor.
   */
  ~IndexedStringNG();

  /**
   * Returns a not reference-counted IndexedStringNG that represents the given index.
   *
   * @warning It is dangerous dealing with indices directly, because it may break
   *          the reference counting logic never stay pure indices to disk
   */
  static IndexedStringNG fromIndex(uint index)
  {
    IndexedStringNG ret;
    ret.m_index = index;
    return ret;
  }

  /**
   * The string is uniquely identified by this index. You can use it for comparison.
   *
   * @warning It is dangerous dealing with indices directly, because it may break the
   *          reference counting logic. never store pure indices to disk
   */
  inline unsigned int index() const
  {
    return m_index;
  }

  bool isEmpty() const
  {
    return m_index == 0;
  }

  /**
   * @note This is relatively expensive: needs a mutex lock, hash lookups, and eventual loading,
   * so avoid it when possible.
   */
  int length() const;

  /**
   * @warning This is relatively expensive: needs a mutex lock, hash lookups, and eventual loading,
   *       so avoid it when possible.
   */
  static int lengthFromIndex(uint index);

  /**
   * Lookup and return the string for this index.
   */
  QString toString() const;

  /**
   * Lookup and return the string for this index and convert it to a KUrl.
   */
  KUrl toUrl() const
  {
    return KUrl(toString());
  }

  /**
   * Fast index-based comparison
   */
  bool operator == ( const IndexedStringNG& rhs ) const
  {
    return m_index == rhs.m_index;
  }

  /**
   * Fast index-based comparison
   */
  bool operator != ( const IndexedStringNG& rhs ) const
  {
    return m_index != rhs.m_index;
  }

  /**
   * Does not compare alphabetically, uses the index for ordering.
   */
  bool operator < ( const IndexedStringNG& rhs ) const
  {
    return m_index < rhs.m_index;
  }

private:
  explicit IndexedStringNG(bool);
  uint m_index;
};

// the following function would need to be exported in case you'd remove the inline keyword.
inline uint qHash( const KDevelop::IndexedStringNG& str )
{
  return str.index();
}

}

/**
 * kDebug() stream operator.  Writes the string to the debug output.
 */
KDEVPLATFORMLANGUAGE_EXPORT QDebug operator<<(QDebug s, const KDevelop::IndexedStringNG& string);

Q_DECLARE_METATYPE(KDevelop::IndexedStringNG);
Q_DECLARE_TYPEINFO(KDevelop::IndexedStringNG, Q_MOVABLE_TYPE);

#endif
