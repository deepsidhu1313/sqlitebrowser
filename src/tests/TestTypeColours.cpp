#include "TestTypeColours.h"

#include "../CondFormat.h"
#include "../Settings.h"
#include "../sqlitedb.h"
#include "../sqlitetablemodel.h"
#include "../sql/Query.h"

#include <QApplication>
#include <QColor>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

QTEST_MAIN(TestTypeColours)

namespace
{

// Colours which are different from each other and from the defaults
const char* const REGULAR_COLOUR = "#101010";
const char* const INTEGER_COLOUR = "#0000ff";
const char* const REAL_COLOUR = "#ff00ff";
const char* const TEXT_COLOUR = "#00aa00";
const char* const NULL_COLOUR = "#aaaaaa";
const char* const BINARY_COLOUR = "#bbbbbb";
const char* const REGULAR_BACKGROUND = "#f0f0f0";

void setSetting(const char* name, const QString& value)
{
    // Do not save these to disk. They are just settings for this test run.
    Settings::setValue("databrowser", name, value, false);
}

}

TestTypeColours::TestTypeColours() = default;
TestTypeColours::~TestTypeColours() = default;

void TestTypeColours::defaultColoursAreValidAndDifferent()
{
    // Colouring the values by their type is something to turn on
    QVERIFY2(!Settings::getValue("databrowser", "colour_by_type").toBool(), "the colours by type should be off by default");

    const QColor integerColour(Settings::getValue("databrowser", "int_fg_colour").toString());
    const QColor realColour(Settings::getValue("databrowser", "real_fg_colour").toString());
    const QColor textColour(Settings::getValue("databrowser", "text_fg_colour").toString());
    QVERIFY2(integerColour.isValid(), "there should be a default colour for integers");
    QVERIFY2(realColour.isValid(), "there should be a default colour for real numbers");
    QVERIFY2(textColour.isValid(), "there should be a default colour for text");
    QVERIFY2(integerColour != realColour, "the default colours for integers and real numbers should be different");
    QVERIFY2(integerColour != textColour, "the default colours for integers and text should be different");
    QVERIFY2(realColour != textColour, "the default colours for real numbers and text should be different");
}

void TestTypeColours::init()
{
    m_dir.reset(new QTemporaryDir());
    QVERIFY(m_dir->isValid());

    m_db.reset(new DBBrowserDB());
    QVERIFY(m_db->create(m_dir->filePath("test.db")));
    QVERIFY(m_db->executeSQL("CREATE TABLE t(i INTEGER, r REAL, n NUMERIC, s TEXT, b BLOB, u)"));
    QVERIFY(m_db->executeSQL("INSERT INTO t VALUES"
                             "(1, 1.5, 2, 'abc', x'00ff', 'zzz'),"
                             "(NULL, NULL, NULL, NULL, NULL, NULL),"
                             "(3, 3.5, 4, 'def', 'plain text', 'zzz')"));
    m_db->updateSchema();
}

void TestTypeColours::cleanup()
{
    // The model has to go before the database it reads from
    m_model.reset();
    m_db.reset();
    m_dir.reset();
}

void TestTypeColours::createModel(bool colourByType)
{
    setSetting("reg_fg_colour", REGULAR_COLOUR);
    setSetting("reg_bg_colour", REGULAR_BACKGROUND);
    setSetting("null_fg_colour", NULL_COLOUR);
    setSetting("bin_fg_colour", BINARY_COLOUR);
    setSetting("int_fg_colour", INTEGER_COLOUR);
    setSetting("real_fg_colour", REAL_COLOUR);
    setSetting("text_fg_colour", TEXT_COLOUR);
    Settings::setValue("databrowser", "colour_by_type", colourByType, false);

    m_model.reset(new SqliteTableModel(*m_db, nullptr, QString(), true));
    QSignalSpy rowCountSpy(m_model.get(), &SqliteTableModel::finishedRowCount);
    m_model->setQuery(sqlb::Query(sqlb::ObjectIdentifier("main", "t")));
    QVERIFY(rowCountSpy.wait(5000) || rowCountSpy.count() > 0);
    QVERIFY(m_model->completeCache());
    QCOMPARE(m_model->rowCount(), 3);
}

QColor TestTypeColours::foreground(int row, int column) const
{
    return m_model->data(m_model->index(row, column), Qt::ForegroundRole).value<QColor>();
}

void TestTypeColours::typesAreNotColouredByDefault_data()
{
    QTest::addColumn<int>("row");
    QTest::addColumn<int>("column");
    QTest::addColumn<QString>("expected");

    QTest::newRow("rowid") << 0 << 0 << QString(REGULAR_COLOUR);
    QTest::newRow("integer") << 0 << 1 << QString(REGULAR_COLOUR);
    QTest::newRow("real") << 0 << 2 << QString(REGULAR_COLOUR);
    QTest::newRow("numeric") << 0 << 3 << QString(REGULAR_COLOUR);
    QTest::newRow("text") << 0 << 4 << QString(REGULAR_COLOUR);
    QTest::newRow("text in blob column") << 2 << 5 << QString(REGULAR_COLOUR);
    QTest::newRow("untyped") << 0 << 6 << QString(REGULAR_COLOUR);
    QTest::newRow("null") << 1 << 1 << QString(NULL_COLOUR);
    QTest::newRow("binary") << 0 << 5 << QString(BINARY_COLOUR);
}

void TestTypeColours::typesAreNotColouredByDefault()
{
    QFETCH(int, row);
    QFETCH(int, column);
    QFETCH(QString, expected);

    createModel(false);
    QCOMPARE(foreground(row, column).name(), QColor(expected).name());
}

void TestTypeColours::cellsGetTheColourOfTheirType_data()
{
    QTest::addColumn<int>("row");
    QTest::addColumn<int>("column");
    QTest::addColumn<QString>("expected");

    // The colour depends on the type of the column
    QTest::newRow("rowid") << 0 << 0 << QString(INTEGER_COLOUR);
    QTest::newRow("integer") << 0 << 1 << QString(INTEGER_COLOUR);
    QTest::newRow("integer in another row") << 2 << 1 << QString(INTEGER_COLOUR);
    QTest::newRow("real") << 0 << 2 << QString(REAL_COLOUR);
    QTest::newRow("numeric") << 0 << 3 << QString(REAL_COLOUR);
    QTest::newRow("text") << 0 << 4 << QString(TEXT_COLOUR);
    QTest::newRow("text in another row") << 2 << 4 << QString(TEXT_COLOUR);

    // Columns without one of these types are not coloured
    QTest::newRow("text in blob column") << 2 << 5 << QString(REGULAR_COLOUR);
    QTest::newRow("untyped") << 0 << 6 << QString(REGULAR_COLOUR);

    // NULL and binary data have their own colours which are more important
    QTest::newRow("null in integer column") << 1 << 1 << QString(NULL_COLOUR);
    QTest::newRow("null in real column") << 1 << 2 << QString(NULL_COLOUR);
    QTest::newRow("null in text column") << 1 << 4 << QString(NULL_COLOUR);
    QTest::newRow("binary") << 0 << 5 << QString(BINARY_COLOUR);
}

void TestTypeColours::cellsGetTheColourOfTheirType()
{
    QFETCH(int, row);
    QFETCH(int, column);
    QFETCH(QString, expected);

    createModel(true);
    QCOMPARE(foreground(row, column).name(), QColor(expected).name());
}

void TestTypeColours::backgroundIsNotChanged()
{
    createModel(true);
    for(int column = 0; column < m_model->columnCount(); ++column)
    {
        const QColor background = m_model->data(m_model->index(0, column), Qt::BackgroundRole).value<QColor>();
        // The binary cell has its own background but this is not what is tested here
        if(column != 5)
            QVERIFY2(background.name() == QColor(REGULAR_BACKGROUND).name(), qPrintable("column " + QString::number(column) + " has the background " + background.name()));
    }
}

void TestTypeColours::conditionalFormatWins()
{
    createModel(true);

    // A format without a condition applies to every value of the column
    m_model->addCondFormat(false, 1, CondFormat(QString(), QColor("#ff0000"), QColor("#00ff00"), QFont()));
    QCOMPARE(foreground(0, 1).name(), QColor("#ff0000").name());
    QCOMPARE(foreground(2, 1).name(), QColor("#ff0000").name());

    // The other columns are not affected
    QCOMPARE(foreground(0, 4).name(), QColor(TEXT_COLOUR).name());
}

void TestTypeColours::changedColoursAreUsedAfterReloading()
{
    createModel(true);
    QCOMPARE(foreground(0, 1).name(), QColor(INTEGER_COLOUR).name());

    setSetting("int_fg_colour", "#123456");
    m_model->reloadSettings();
    QCOMPARE(foreground(0, 1).name(), QColor("#123456").name());

    // And the colours go away again when the setting is turned off
    Settings::setValue("databrowser", "colour_by_type", false, false);
    m_model->reloadSettings();
    QCOMPARE(foreground(0, 1).name(), QColor(REGULAR_COLOUR).name());
}
