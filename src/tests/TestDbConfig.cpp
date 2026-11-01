#include "TestDbConfig.h"

#include "../sqlitedb.h"
#include "../sqlite.h"

#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <string>

QTEST_MAIN(TestDbConfig)

Q_DECLARE_METATYPE(DBBrowserDB::DbConfigOption)

namespace
{

// The value of a single-value query, or an explanatory marker if the query failed
QString singleValue(DBBrowserDB& db, const std::string& sql)
{
    return QString::fromUtf8(db.querySingleValueFromDb(sql, false, DBBrowserDB::Wait));
}

// All options used here are known to SQLite 3.31.0 and newer. Whether the library is new enough is decided independently of the code
// under test, so that a broken getDbConfig() can't hide behind a skipped test.
#define REQUIRE_SQLITE_DBCONFIG() \
    do { \
        if(sqlite3_libversion_number() < 3031000) \
            QSKIP(qPrintable(QString("SQLite %1 is too old for these options").arg(QString::fromUtf8(sqlite3_libversion())))); \
    } while(false)

// Reads an option and reports the option's number and the SQLite version if that fails
bool readOption(DBBrowserDB& db, DBBrowserDB::DbConfigOption option, bool& value, QString& error)
{
    if(db.getDbConfig(option, value))
        return true;
    error = QString("getDbConfig() failed for option number %1 (SQLite %2)").arg(static_cast<int>(option)).arg(QString::fromUtf8(sqlite3_libversion()));
    return false;
}

}

void TestDbConfig::noDatabaseOpen()
{
    DBBrowserDB db;
    bool value = true;
    QVERIFY2(!db.getDbConfig(DBBrowserDB::DbConfigOption::DqsDml, value), "getDbConfig() must fail without an open database");
    QVERIFY2(!db.setDbConfig(DBBrowserDB::DbConfigOption::DqsDml, false), "setDbConfig() must fail without an open database");
}

void TestDbConfig::toggleOptions_data()
{
    QTest::addColumn<DBBrowserDB::DbConfigOption>("option");

    QTest::newRow("enable triggers") << DBBrowserDB::DbConfigOption::EnableTrigger;
    QTest::newRow("enable fts3 tokenizer") << DBBrowserDB::DbConfigOption::EnableFts3Tokenizer;
    QTest::newRow("defensive") << DBBrowserDB::DbConfigOption::Defensive;
    QTest::newRow("dqs dml") << DBBrowserDB::DbConfigOption::DqsDml;
    QTest::newRow("dqs ddl") << DBBrowserDB::DbConfigOption::DqsDdl;
    QTest::newRow("trusted schema") << DBBrowserDB::DbConfigOption::TrustedSchema;
}

void TestDbConfig::toggleOptions()
{
    REQUIRE_SQLITE_DBCONFIG();
    QFETCH(DBBrowserDB::DbConfigOption, option);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    DBBrowserDB db;
    QVERIFY(db.create(dir.filePath("test.db")));

    bool initial = false;
    QString error;
    QVERIFY2(readOption(db, option, initial, error), qPrintable(error));

    // Flip the option and read it back, then restore it
    QVERIFY2(db.setDbConfig(option, !initial), qPrintable(QString("setDbConfig(%1) failed").arg(!initial)));
    bool value = initial;
    QVERIFY2(db.getDbConfig(option, value), "getDbConfig() failed after changing the option");
    QVERIFY2(value == !initial, qPrintable(QString("option was %1, set to %2 but read back as %3").arg(initial).arg(!initial).arg(value)));

    QVERIFY2(db.setDbConfig(option, initial), qPrintable(QString("setDbConfig(%1) failed when restoring").arg(initial)));
    QVERIFY(db.getDbConfig(option, value));
    QVERIFY2(value == initial, qPrintable(QString("option should be back at %1 but is %2").arg(initial).arg(value)));
}

void TestDbConfig::doubleQuotedStringsInDml()
{
    REQUIRE_SQLITE_DBCONFIG();

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    DBBrowserDB db;
    QVERIFY(db.create(dir.filePath("test.db")));
    QVERIFY(db.executeSQL("CREATE TABLE t(a TEXT)"));

    bool value;
    QString error;
    QVERIFY2(readOption(db, DBBrowserDB::DbConfigOption::DqsDml, value, error), qPrintable(error));

    // With the misfeature turned off a double-quoted word that is not a column name is an error...
    QVERIFY(db.setDbConfig(DBBrowserDB::DbConfigOption::DqsDml, false));
    QVERIFY2(!db.executeSQL("INSERT INTO t VALUES(\"abc\")"), "a double-quoted string literal was accepted although DQS_DML is off");
    // ... while double-quoted identifiers keep working
    QVERIFY2(db.executeSQL("INSERT INTO t(\"a\") VALUES('x')"), qPrintable("double-quoted identifier rejected: " + db.lastError()));

    // ... and with it turned on it is a string literal
    QVERIFY(db.setDbConfig(DBBrowserDB::DbConfigOption::DqsDml, true));
    QVERIFY2(db.executeSQL("INSERT INTO t VALUES(\"abc\")"), qPrintable("double-quoted string literal rejected although DQS_DML is on: " + db.lastError()));
    QCOMPARE(singleValue(db, "SELECT group_concat(a) FROM (SELECT a FROM t ORDER BY rowid)"), QString("x,abc"));
}

void TestDbConfig::doubleQuotedStringsInDdl()
{
    REQUIRE_SQLITE_DBCONFIG();

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    DBBrowserDB db;
    QVERIFY(db.create(dir.filePath("test.db")));

    bool value;
    QString error;
    QVERIFY2(readOption(db, DBBrowserDB::DbConfigOption::DqsDdl, value, error), qPrintable(error));

    // A CHECK constraint is parsed as DDL
    QVERIFY(db.setDbConfig(DBBrowserDB::DbConfigOption::DqsDdl, false));
    QVERIFY2(!db.executeSQL("CREATE TABLE t(a TEXT, CHECK(a <> \"x\"))"), "a double-quoted string literal was accepted although DQS_DDL is off");

    QVERIFY(db.setDbConfig(DBBrowserDB::DbConfigOption::DqsDdl, true));
    QVERIFY2(db.executeSQL("CREATE TABLE t(a TEXT, CHECK(a <> \"x\"))"), qPrintable("double-quoted string literal rejected although DQS_DDL is on: " + db.lastError()));
}

void TestDbConfig::defensiveMode()
{
    REQUIRE_SQLITE_DBCONFIG();

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    DBBrowserDB db;
    QVERIFY(db.create(dir.filePath("test.db")));

    bool value;
    QString error;
    QVERIFY2(readOption(db, DBBrowserDB::DbConfigOption::Defensive, value, error), qPrintable(error));

    // Writing to the schema table is only possible with writable_schema turned on and defensive mode turned off. The statement
    // doesn't match any row, so nothing is modified when it is accepted.
    QVERIFY(db.setDbConfig(DBBrowserDB::DbConfigOption::Defensive, false));
    QVERIFY2(db.executeSQL("PRAGMA writable_schema = ON"), qPrintable("could not turn on writable_schema: " + db.lastError()));
    QVERIFY2(db.executeSQL("UPDATE sqlite_master SET name = name WHERE 0"), qPrintable("schema table not writable although defensive mode is off: " + db.lastError()));

    QVERIFY(db.setDbConfig(DBBrowserDB::DbConfigOption::Defensive, true));
    QVERIFY2(!db.executeSQL("UPDATE sqlite_master SET name = name WHERE 0"), "schema table is writable although defensive mode is on");
}

void TestDbConfig::triggersCanBeDisabled()
{
    REQUIRE_SQLITE_DBCONFIG();

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    DBBrowserDB db;
    QVERIFY(db.create(dir.filePath("test.db")));
    QVERIFY(db.executeSQL("CREATE TABLE t(a)"));
    QVERIFY(db.executeSQL("CREATE TABLE log(m)"));
    QVERIFY(db.executeSQL("CREATE TRIGGER trg AFTER INSERT ON t BEGIN INSERT INTO log VALUES('fired'); END"));

    bool value;
    QString error;
    QVERIFY2(readOption(db, DBBrowserDB::DbConfigOption::EnableTrigger, value, error), qPrintable(error));

    QVERIFY(db.setDbConfig(DBBrowserDB::DbConfigOption::EnableTrigger, false));
    QVERIFY(db.executeSQL("INSERT INTO t VALUES(1)"));
    QCOMPARE(singleValue(db, "SELECT count(*) FROM log"), QString("0"));

    QVERIFY(db.setDbConfig(DBBrowserDB::DbConfigOption::EnableTrigger, true));
    QVERIFY(db.executeSQL("INSERT INTO t VALUES(2)"));
    QCOMPARE(singleValue(db, "SELECT count(*) FROM log"), QString("1"));
}

void TestDbConfig::optionsAreNotStoredInTheFile()
{
    REQUIRE_SQLITE_DBCONFIG();

    // The options belong to the connection. A new connection to the same file starts with SQLite's defaults again.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString file = dir.filePath("test.db");

    bool defaultValue = true;
    {
        DBBrowserDB db;
        QVERIFY(db.create(file));
        QString error;
        QVERIFY2(readOption(db, DBBrowserDB::DbConfigOption::DqsDml, defaultValue, error), qPrintable(error));
        QVERIFY(db.setDbConfig(DBBrowserDB::DbConfigOption::DqsDml, !defaultValue));
        QVERIFY(db.close());
    }

    DBBrowserDB db;
    QVERIFY(db.open(file));
    bool value = !defaultValue;
    QVERIFY(db.getDbConfig(DBBrowserDB::DbConfigOption::DqsDml, value));
    QVERIFY2(value == defaultValue, "the option survived reopening the database");
}
