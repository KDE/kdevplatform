/*
 * This file is part of KDevelop
 * Copyright 2012 Milian Wolff <mail@milianw.de>
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

#include "benchindexedstring.h"

#include <qtest_kde.h>

#include <tests/testcore.h>
#include <tests/autotestshell.h>

#include <language/duchain/indexedstring.h>

QTEST_KDEMAIN(BenchIndexedString, NoGUI);

using namespace KDevelop;

void BenchIndexedString::initTestCase()
{
  AutoTestShell::init();
  TestCore::initialize(Core::NoUi);
}

void BenchIndexedString::cleanupTestCase()
{
  TestCore::shutdown();
}

QVector<QString> generateData()
{
  QVector<QString> data;
  static const int NUM_ITEMS = 100000;
  data.resize(NUM_ITEMS);
  for(int i = 0; i < NUM_ITEMS; ++i) {
    data[i] = QString("/foo/%1").arg(i);
  }
  return data;
}

void BenchIndexedString::index()
{
  QVector<QString> data = generateData();
  QBENCHMARK {
    foreach(const QString& item, data) {
      IndexedString idx(item);
      Q_UNUSED(idx);
    }
  }
}

QVector<uint> setupTest()
{
  QVector<QString> data = generateData();
  QVector<uint> indices;
  indices.reserve(data.size());
  foreach(const QString& item, data) {
    IndexedString idx(item);
    indices << idx.index();
  }
  return indices;
}

void BenchIndexedString::length()
{
  QVector<uint> indices = setupTest();
  QBENCHMARK {
    foreach(uint index, indices) {
      IndexedString str = IndexedString::fromIndex(index);
      str.length();
    }
  }
}

void BenchIndexedString::qstring()
{
  QVector<uint> indices = setupTest();
  QBENCHMARK {
    foreach(uint index, indices) {
      IndexedString str = IndexedString::fromIndex(index);
      str.toString();
    }
  }
}

void BenchIndexedString::kurl()
{
  QVector<uint> indices = setupTest();
  QBENCHMARK {
    foreach(uint index, indices) {
      IndexedString str = IndexedString::fromIndex(index);
      str.toUrl();
    }
  }
}

void BenchIndexedString::hashString()
{
  QVector<QString> data = generateData();
  QBENCHMARK {
    foreach(const QString& item, data) {
      qHash(item);
    }
  }
}

void BenchIndexedString::hashIndexed()
{
  QVector<uint> indices = setupTest();
  QBENCHMARK {
    foreach(uint index, indices) {
      qHash(IndexedString::fromIndex(index));
    }
  }
}

void BenchIndexedString::qSet()
{
  QVector<uint> indices = setupTest();
  QSet<IndexedString> set;
  QBENCHMARK {
    foreach(uint index, indices) {
      set.insert(IndexedString::fromIndex(index));
    }
  }
}

void BenchIndexedString::test()
{
  QFETCH(QString, data);

  IndexedString indexed(data);
  QCOMPARE(indexed.toString(), data);
  QCOMPARE(indexed.length(), data.length());
}

void BenchIndexedString::test_data()
{
  QTest::addColumn<QString>("data");

  QTest::newRow("empty") << QString();
  QTest::newRow("char-ascii") << QString("a");
  QTest::newRow("char-utf8") << QString::fromUtf8("ä");
  QTest::newRow("string-ascii") << QString::fromAscii("asdf()?=");
  QTest::newRow("string-utf8") << QString::fromUtf8("æſðđäöü");
}

#include "benchindexedstring.moc"
