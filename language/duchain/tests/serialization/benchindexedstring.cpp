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

void BenchIndexedString::length()
{
  QVector<QString> data = generateData();
  QVector<uint> indices;
  indices.reserve(data.size());
  foreach(const QString& item, data) {
    IndexedString idx(item);
    indices << idx.index();
  }
  QBENCHMARK {
    foreach(uint index, indices) {
      IndexedString str = IndexedString::fromIndex(index);
      str.length();
    }
  }
}

void BenchIndexedString::qstring()
{
  QVector<QString> data = generateData();
  QVector<uint> indices;
  indices.reserve(data.size());
  foreach(const QString& item, data) {
    IndexedString idx(item);
    indices << idx.index();
  }
  QBENCHMARK {
    foreach(uint index, indices) {
      IndexedString str = IndexedString::fromIndex(index);
      str.str();
    }
  }
}

void BenchIndexedString::hash()
{
  QVector<QString> data = generateData();
  QBENCHMARK {
    foreach(const QString& item, data) {
      const QByteArray strArr = item.toUtf8();
      IndexedString::hashString(strArr.constData(), strArr.length());
    }
  }
}

#include "benchindexedstring.moc"
