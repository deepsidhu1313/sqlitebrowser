#include "TestBusyTimeout.h"

#include "../sqlitedb.h"
#include "../Settings.h"
#include "../sqlite.h"

#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest/QtTest>

#include <atomic>

QTEST_MAIN(TestBusyTimeout)

namespace
{

// Sets the busy timeout setting for the lifetime of the object and puts the default back afterwards, even if a test fails half way
// through. The setting is only changed in the cache of the current process, nothing is written to the user's settings file.
class BusyTimeoutSetting
{
public:
    explicit BusyTimeoutSetting(int milliseconds) { set(milliseconds); }
    ~BusyTimeoutSetting() { set(0); }

private:
    static void set(int milliseconds) { Settings::setValue("db", "busytimeout", milliseconds, false); }
};

// Holds a write lock on a database file from a second connection until release() is called or the given time has passed
class LockHolder : public QThread
{
public:
    LockHolder(const QString& file, int maxHoldMilliseconds) : m_file(file), m_maxHold(maxHoldMilliseconds), m_started(false), m_ok(false), m_release(false) {}
    ~LockHolder() override
    {
        release();
        wait();
    }

    // Returns true as soon as the write lock is held
    bool waitUntilLocked() const
    {
        QElapsedTimer timer;
        timer.start();
        while(!m_started && timer.elapsed() < 10000)
            QThread::msleep(5);
        return m_started && m_ok;
    }

    void release() { m_release = true; }

protected:
    void run() override
    {
        sqlite3* db = nullptr;
        if(sqlite3_open_v2(m_file.toUtf8().constData(), &db, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK &&
           sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) == SQLITE_OK)
        {
            m_ok = true;
            m_started = true;

            QElapsedTimer timer;
            timer.start();
            while(!m_release && timer.elapsed() < m_maxHold)
                QThread::msleep(5);

            sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
        } else {
            m_started = true;
        }
        sqlite3_close(db);
    }

private:
    QString m_file;
    int m_maxHold;
    std::atomic<bool> m_started;
    std::atomic<bool> m_ok;
    std::atomic<bool> m_release;
};

}

void TestBusyTimeout::defaultIsNoTimeout()
{
    // Without any configuration the behaviour is the one SQLite has by default: fail immediately when the database is locked
    QCOMPARE(Settings::getValue("db", "busytimeout").toInt(), 0);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    DBBrowserDB db;
    QVERIFY(db.create(dir.filePath("test.db")));
    QCOMPARE(db.getPragma("busy_timeout"), QString("0"));
}

void TestBusyTimeout::timeoutIsAppliedOnOpen_data()
{
    QTest::addColumn<int>("milliseconds");

    QTest::newRow("none") << 0;
    QTest::newRow("one second") << 1000;
    QTest::newRow("odd value") << 4321;
}

void TestBusyTimeout::timeoutIsAppliedOnOpen()
{
    QFETCH(int, milliseconds);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString file = dir.filePath("test.db");
    {
        DBBrowserDB db;
        QVERIFY(db.create(file));
    }

    BusyTimeoutSetting setting(milliseconds);
    DBBrowserDB db;
    QVERIFY(db.open(file));
    QCOMPARE(db.getPragma("busy_timeout"), QString::number(milliseconds));
}

void TestBusyTimeout::timeoutIsAppliedOnCreate()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    BusyTimeoutSetting setting(2500);
    DBBrowserDB db;
    QVERIFY(db.create(dir.filePath("test.db")));
    QCOMPARE(db.getPragma("busy_timeout"), QString("2500"));
}

void TestBusyTimeout::lockedDatabaseFailsImmediatelyByDefault()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString file = dir.filePath("test.db");

    DBBrowserDB db;
    QVERIFY(db.create(file));
    QVERIFY(db.executeSQL("CREATE TABLE t(a)"));
    QVERIFY(db.releaseAllSavepoints());

    // The other connection keeps the lock until we are done, so the write can only succeed by waiting for it
    LockHolder holder(file, 60000);
    holder.start();
    QVERIFY2(holder.waitUntilLocked(), "the second connection could not lock the database");

    const bool inserted = db.executeSQL("INSERT INTO t VALUES(1)");
    holder.release();
    holder.wait();

    QVERIFY2(!inserted, "the write succeeded although another connection held the write lock");
    QVERIFY2(db.lastError().contains("locked"), qPrintable("unexpected error: " + db.lastError()));
}

void TestBusyTimeout::lockedDatabaseIsWaitedForWithTimeout()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString file = dir.filePath("test.db");

    BusyTimeoutSetting setting(20000);
    DBBrowserDB db;
    QVERIFY(db.create(file));
    QVERIFY(db.executeSQL("CREATE TABLE t(a)"));
    QVERIFY(db.releaseAllSavepoints());

    // The other connection gives up its lock after a short time, long before the timeout is over
    LockHolder holder(file, 400);
    holder.start();
    QVERIFY2(holder.waitUntilLocked(), "the second connection could not lock the database");

    const bool inserted = db.executeSQL("INSERT INTO t VALUES(1)");
    holder.wait();

    QVERIFY2(inserted, qPrintable("the write failed instead of waiting for the lock: " + db.lastError()));
    QCOMPARE(QString::fromUtf8(db.querySingleValueFromDb("SELECT count(*) FROM t", false, DBBrowserDB::Wait)), QString("1"));
}
