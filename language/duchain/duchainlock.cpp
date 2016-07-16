/* This file is part of KDevelop
    Copyright 2007 Kris Wong <kris.p.wong@gmail.com>
    Copyright 2007 Hamish Rodda <rodda@kde.org>
   Copyright 2007-2009 David Nolden <david.nolden.kdevelop@art-master.de>
   Copyright 2013 Milian Wolff <mail@milianw.de>

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

#include "duchainlock.h"
#include "duchain.h"

#include <QThread>
#include <QThreadStorage>
#include <QElapsedTimer>


///@todo Always prefer exactly that lock that is requested by the thread that has the foreground mutex,
///           to reduce the amount of UI blocking.

namespace KDevelop
{

class DUChainLockPrivate
{
public:
  DUChainLockPrivate()
    : m_writer(0)
    , m_writerRecursion(0)
    , m_totalReaderRecursion(0)
  { }

  int ownReaderRecursion() const
  {
    return m_readerRecursion.localData();
  }

  void changeOwnReaderRecursion(int difference)
  {
    m_readerRecursion.localData() += difference;
    Q_ASSERT(m_readerRecursion.localData() >= 0);
    m_totalReaderRecursion.fetchAndAddOrdered(difference);
  }

  ///Holds the writer that currently has the write-lock, or zero. Is protected by m_writerRecursion.
  QAtomicPointer<QThread> m_writer;

  ///How often is the chain write-locked by the writer? This value protects m_writer,
  ///m_writer may only be changed by the thread that successfully increases this value from 0 to 1
  QAtomicInt m_writerRecursion;
  ///How often is the chain read-locked recursively by all readers? Should be sum of all m_readerRecursion values
  QAtomicInt m_totalReaderRecursion;

  QThreadStorage<int> m_readerRecursion;
};

DUChainLock::DUChainLock()
  : d(new DUChainLockPrivate)
{
}

DUChainLock::~DUChainLock()
{
  delete d;
}

bool DUChainLock::lockForRead(unsigned int timeout)
{
  ///Step 1: Increase the own reader-recursion. This will make sure no further write-locks will succeed
  d->changeOwnReaderRecursion(1);

  QThread* w = d->m_writer.loadAcquire();
  if (w == 0 || w == QThread::currentThread()) {
    //Successful lock: Either there is no writer, or we hold the write-lock by ourselves
  } else {
    ///Step 2: Start spinning until there is no writer any more

    QElapsedTimer t;
    if (timeout) {
      t.start();
    }

    while (d->m_writer.loadAcquire()) {
      if (!timeout || t.elapsed() < timeout) {
        QThread::yieldCurrentThread();
      } else {
        //Fail!
        d->changeOwnReaderRecursion(-1);
        return false;
      }
    }
  }

  return true;
}

void DUChainLock::releaseReadLock()
{
  d->changeOwnReaderRecursion(-1);
}

bool DUChainLock::currentThreadHasReadLock()
{
  return (bool)d->ownReaderRecursion();
}

bool DUChainLock::lockForWrite(uint timeout)
{
  //It is not allowed to acquire a write-lock while holding read-lock

  Q_ASSERT(d->ownReaderRecursion() == 0);

  if (d->m_writer.load() == QThread::currentThread()) {
    //We already hold the write lock, just increase the recursion count and return
    d->m_writerRecursion.fetchAndAddRelaxed(1);
    return true;
  }

  QElapsedTimer t;
  if (timeout) {
    t.start();
  }

  while (1) {
    //Try acquiring the write-lcok
    if (d->m_totalReaderRecursion.load() == 0 && d->m_writerRecursion.testAndSetOrdered(0, 1)) {
      //Now we can be sure that there is no other writer, as we have increased m_writerRecursion from 0 to 1
      d->m_writer = QThread::currentThread();
      if (d->m_totalReaderRecursion.load() == 0) {
        //There is still no readers, we have successfully acquired a write-lock
        return true;
      } else {
        //There may be readers.. we have to continue spinning
        d->m_writer = 0;
        d->m_writerRecursion = 0;
      }
    }

    if (!timeout || t.elapsed() < timeout) {
      QThread::yieldCurrentThread();
    } else {
      //Fail!
      return false;
    }
  }

  return false;
}

void DUChainLock::releaseWriteLock()
{
  Q_ASSERT(currentThreadHasWriteLock());

  //The order is important here, m_writerRecursion protects m_writer

  //TODO: could testAndSet here
  if (d->m_writerRecursion.load() == 1) {
    d->m_writer = 0;
    d->m_writerRecursion = 0;
  } else {
    d->m_writerRecursion.fetchAndAddOrdered(-1);
  }
}

bool DUChainLock::currentThreadHasWriteLock()
{
  return d->m_writer.load() == QThread::currentThread();
}

DUChainReadLocker::DUChainReadLocker(DUChainLock* duChainLock, uint timeout)
  : m_lock(duChainLock ? duChainLock : DUChain::lock())
  , m_locked(false)
  , m_timeout(timeout)
{
  lock();
}

DUChainReadLocker::~DUChainReadLocker()
{
  unlock();
}

bool DUChainReadLocker::locked() const
{
  return m_locked;
}

bool DUChainReadLocker::lock()
{
  if (m_locked) {
    return true;
  }

  bool l = false;
  if (m_lock) {
    l = m_lock->lockForRead(m_timeout);
    Q_ASSERT(m_timeout || l);
  };

  m_locked = l;

  return l;
}

void DUChainReadLocker::unlock()
{
  if (m_locked && m_lock) {
    m_lock->releaseReadLock();
    m_locked = false;
  }
}

DUChainWriteLocker::DUChainWriteLocker(DUChainLock* duChainLock, uint timeout)
  : m_lock(duChainLock ? duChainLock : DUChain::lock())
  , m_locked(false)
  , m_timeout(timeout)
{
  lock();
}

DUChainWriteLocker::~DUChainWriteLocker()
{
  unlock();
}

bool DUChainWriteLocker::lock()
{
  if (m_locked) {
    return true;
  }

  bool l = false;
  if (m_lock) {
    l = m_lock->lockForWrite(m_timeout);
    Q_ASSERT(m_timeout || l);
  };

  m_locked = l;

  return l;
}

bool DUChainWriteLocker::locked() const
{
  return m_locked;
}

void DUChainWriteLocker::unlock()
{
  if (m_locked && m_lock) {
    m_lock->releaseWriteLock();
    m_locked = false;
  }
}

}
