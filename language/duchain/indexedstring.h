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

#ifndef INDEXED_STRING_H
#define INDEXED_STRING_H

//krazy:excludeall=dpointer,inline

#include <QtCore/QMetaType>

#include <KUrl>

#include <language/languageexport.h>

namespace KDevelop {

/**
 * This class gives an indexed, i.e. serialized version of a QString.
 *
 * You can use this to persistently store strings or efficientlly store them
 * in a hash or similar.
 *
 * @note Serializing a QString followed by a deserialization will break the
 * implicit sharing. Thus alwasy prefer to use existing QStrings directly
 * if possible. Alternatively, only use the deserialized strings temporarily.
 *
 * @note This string does "disk reference-counting", which means that reference-counts are maintainted,
 * but only when the string is in a disk-stored location. The file referencecounting.h is used
 * to manage this condition.
 *
 * Whenever reference-counting is enabled for a range that contains the IndexedString, it will
 * manipulate the reference-counts.
 *
 * The duchain storage mechanisms automatically are about correctly managing that condition,
 * so you don't need to care, and can just use this class in every duchain data type without
 * restrictions.
 *
 * @warning Do not use IndexedString after QCoreApplication::aboutToQuit() has been emitted,
 * items that are not disk-referenced will be invalid at that point.
 *
 * @note Empty strings have an index of zero.
 *
 * @note Strings of length one are not put into the repository, but are encoded directly within
 * the index: They are encoded like @c 0xffff00bb where @c bb is the byte of the character.
 */
class KDEVPLATFORMLANGUAGE_EXPORT IndexedString
{
public:
  /**
   * Create an empty IndexedString.
   */
  inline IndexedString()
  : m_index(0)
  {
  }

  /**
   * Index the given char @p c.
   */
  explicit IndexedString(QChar c);

  /**
   * Index the given @p string.
   */
  explicit IndexedString(const QString& string);

  /**
   * Index the given @p KUrl.
   *
   * NOTE: This is expensive.
   *
   * TODO: This should be replaced by a IndexedPath eventually.
   */
  explicit IndexedString(const KUrl& url);

  /**
   * Copy constructor.
   */
  IndexedString(const IndexedString& other);

  /**
   * Assignment operator.
   */
  IndexedString& operator=(const IndexedString&);

  /**
   * Destructor.
   */
  ~IndexedString();

  /**
   * Returns a not reference-counted IndexedString that represents the given index.
   *
   * @warning It is dangerous dealing with indices directly, because it may break
   *          the reference counting logic never stay pure indices to disk
   */
  static IndexedString fromIndex(uint index)
  {
    IndexedString ret;
    ret.m_index = index;
    return ret;
  }

  /**
   * The string is uniquely identified by this index. You can use it for comparison.
   *
   * @warning It is dangerous dealing with indices directly, because it may break the
   *          reference counting logic. never store pure indices to disk
   */
  inline uint index() const
  {
    return m_index;
  }

  /**
   * The index can directly be used as a non-cryptographic hash.
   */
  inline uint hash() const
  {
    return m_index;
  }

  /**
   * @return true when this string is is empty, false otherwise.
   */
  inline bool isEmpty() const
  {
    return m_index == 0;
  }

  /**
   * Check whether this IndexedString is valid, i.e. not empty.
   */
  inline bool isValid() const
  {
    return !isEmpty();
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

  KDE_DEPRECATED QString str() const
  {
    return toString();
  }

  /**
   * Lookup and return the string for this index and convert it to a KUrl.
   *
   * NOTE: This is expensive.
   *
   * TODO: This should be replaced by a IndexedPath eventually.
   */
  KUrl toUrl() const
  {
    return KUrl(toString());
  }

  /**
   * Fast index-based comparison
   */
  bool operator == ( const IndexedString& rhs ) const
  {
    return m_index == rhs.m_index;
  }

  /**
   * Fast index-based comparison
   */
  bool operator != ( const IndexedString& rhs ) const
  {
    return m_index != rhs.m_index;
  }

  /**
   * Does not compare alphabetically, uses the index for ordering.
   */
  bool operator < ( const IndexedString& rhs ) const
  {
    return m_index < rhs.m_index;
  }

private:
  explicit IndexedString(bool);
  uint m_index;
};

/**
 * Hash function for Qt containers like QHash/QSet.
 */
inline uint qHash( const KDevelop::IndexedString& str )
{
  return str.hash();
}

}

/**
 * qDebug() stream operator: Writes the string to the debug output.
 */
KDEVPLATFORMLANGUAGE_EXPORT QDebug operator<<(QDebug s, const KDevelop::IndexedString& string);

Q_DECLARE_METATYPE(KDevelop::IndexedString);
Q_DECLARE_TYPEINFO(KDevelop::IndexedString, Q_MOVABLE_TYPE);

#endif // INDEXED_STRING_H
