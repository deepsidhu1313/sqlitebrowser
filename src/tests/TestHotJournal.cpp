#include "TestHotJournal.h"

#include "../sqlitedb.h"
#include "../sqlite.h"

#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

QTEST_MAIN(TestHotJournal)

namespace
{

const int NumberOfRows = 200;

QString execute(sqlite3* db, const char* sql)
{
    char* message = nullptr;
    if(sqlite3_exec(db, sql, nullptr, nullptr, &message) == SQLITE_OK)
        return QString();

    const QString error = QString("%1 failed: %2").arg(QString::fromUtf8(sql), message ? QString::fromUtf8(message) : QString("unknown error"));
    sqlite3_free(message);
    return error;
}

// Copies a file that may be open in SQLite. QFile::copy() can't be used for this because on Windows it doesn't read files which
// another process has opened for writing.
bool copyOpenFile(const QString& from, const QString& to)
{
    QFile source(from);
    QFile destination(to);
    if(!source.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly))
        return false;

    const QByteArray data = source.readAll();
    const bool written = destination.write(data) == data.size();
    destination.close();
    return written;
}

// Creates a database with NumberOfRows rows plus a hot journal next to it, the way a program that stopped in the middle of
// a transaction leaves them: the database file contains pages of the unfinished transaction and the journal the original pages.
// The files are copied while the transaction is still going on. Returns an error message or an empty string.
QString createHotJournal(const QString& path)
{
    const QString source = path + "-source";
    sqlite3* db = nullptr;
    if(sqlite3_open(source.toUtf8().constData(), &db) != SQLITE_OK)
        return "Could not create the database";

    QString error = execute(db, "CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT)");
    if(error.isEmpty())
        error = execute(db, QString("WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<%1) "
                                    "INSERT INTO t(v) SELECT 'row ' || x FROM c").arg(NumberOfRows).toUtf8().constData());
    // With a cache this small the changed pages have to be written to the database file before the transaction ends
    if(error.isEmpty())
        error = execute(db, "PRAGMA cache_size=10");
    if(error.isEmpty())
        error = execute(db, "BEGIN");
    if(error.isEmpty())
        error = execute(db, "UPDATE t SET v = v || '-changed-' || hex(randomblob(2000))");

    if(error.isEmpty() && !copyOpenFile(source, path))
        error = "Could not copy the database";
    if(error.isEmpty() && !copyOpenFile(source + "-journal", path + "-journal"))
        error = "The unfinished transaction did not leave a journal behind";

    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    sqlite3_close(db);
    QFile::remove(source);

    if(error.isEmpty())
    {
        // A journal is hot when it starts with this magic number
        QFile journal(path + "-journal");
        if(!journal.open(QIODevice::ReadOnly) || journal.read(8) != QByteArray("\xd9\xd5\x05\xf9\x20\xa1\x63\xd7", 8))
            error = "The journal is not a hot one";
    }
    return error;
}

QString queryText(DBBrowserDB& db, const std::string& sql)
{
    return QString::fromUtf8(db.querySingleValueFromDb(sql, false, DBBrowserDB::Wait));
}

}

void TestHotJournal::openRecoversFromHotJournal()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("crashed.db");
    const QString error = createHotJournal(path);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    DBBrowserDB db;
    QVERIFY2(db.open(path), qPrintable("Opening a database with a hot journal failed: " + db.lastError()));

    // The unfinished transaction is rolled back and the rows are as they were before it
    QCOMPARE(queryText(db, "SELECT count(*) FROM t"), QString::number(NumberOfRows));
    QCOMPARE(queryText(db, "SELECT count(*) FROM t WHERE v LIKE 'row %' AND v NOT LIKE '%changed%'"), QString::number(NumberOfRows));
    QCOMPARE(queryText(db, "PRAGMA integrity_check"), QString("ok"));
    QVERIFY2(!QFile::exists(path + "-journal"), "The journal was not used up");
    db.close();
}

void TestHotJournal::openReadOnlyExplainsHotJournal()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("crashed.db");
    const QString error = createHotJournal(path);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    // SQLite can't read such a database without rolling it back, which a read-only connection can't do. Rolling it back
    // would modify the file though, which is not what the user asked for. So the user is told what is going on.
    DBBrowserDB db;
    QVERIFY2(!db.open(path, true), "Opening a database with a hot journal read-only worked");
    QVERIFY2(db.lastError().contains("journal"), qPrintable("The error message doesn't mention the journal: " + db.lastError()));
    QVERIFY2(QFile::exists(path + "-journal"), "The journal was used although the database was opened read-only");

    // But it can be opened normally afterwards
    QVERIFY2(db.open(path), qPrintable("Opening the database with write access failed: " + db.lastError()));
    QCOMPARE(queryText(db, "SELECT count(*) FROM t WHERE v NOT LIKE '%changed%'"), QString::number(NumberOfRows));
    db.close();
}

void TestHotJournal::emptyLeftoverJournalIsHarmless()
{
    // The journal modes TRUNCATE and PERSIST leave an empty journal behind, which is not hot
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("normal.db");

    sqlite3* raw = nullptr;
    QVERIFY(sqlite3_open(path.toUtf8().constData(), &raw) == SQLITE_OK);
    QVERIFY(execute(raw, "CREATE TABLE t(a); INSERT INTO t VALUES(1),(2),(3)").isEmpty());
    sqlite3_close(raw);
    QFile journal(path + "-journal");
    QVERIFY(journal.open(QIODevice::WriteOnly));
    journal.close();

    DBBrowserDB writable;
    QVERIFY2(writable.open(path), qPrintable(writable.lastError()));
    QCOMPARE(queryText(writable, "SELECT count(*) FROM t"), QString("3"));
    writable.close();

    DBBrowserDB readOnly;
    QVERIFY2(readOnly.open(path, true), qPrintable(readOnly.lastError()));
    QCOMPARE(queryText(readOnly, "SELECT count(*) FROM t"), QString("3"));
    readOnly.close();
}
