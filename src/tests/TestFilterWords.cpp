#include "TestFilterWords.h"

#include "../CondFormat.h"
#include "../Settings.h"
#include "../sqlitedb.h"
#include "../sqlitetablemodel.h"
#include "../sql/Query.h"

#include <QApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <sqlite3.h>

QTEST_MAIN(TestFilterWords)

namespace
{

const char* const SPLIT_WORDS_SETTING = "filter_split_words";

void setSplitWords(bool enable)
{
    // Do not save this to disk. It is just a setting for this test run.
    Settings::setValue("databrowser", SPLIT_WORDS_SETTING, enable, false);
}

QStringList toStrings(const std::vector<std::string>& conditions)
{
    QStringList result;
    for(const std::string& condition : conditions)
        result << QString::fromStdString(condition);
    return result;
}

}

// The members are pointers to types which are only forward declared in the header, so the (de)constructor is defined here
TestFilterWords::TestFilterWords() = default;
TestFilterWords::~TestFilterWords() = default;

void TestFilterWords::init()
{
    setSplitWords(false);
    Settings::setValue("databrowser", "filter_escape", "\\", false);

    m_dir.reset(new QTemporaryDir());
    QVERIFY(m_dir->isValid());

    m_db.reset(new DBBrowserDB());
    QVERIFY(m_db->create(m_dir->filePath("test.db")));
    QVERIFY(m_db->executeSQL("CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT)"));
    QVERIFY(m_db->executeSQL("INSERT INTO t VALUES"
                             "(1,'the quick brown fox'),"
                             "(2,'brown dog'),"
                             "(3,'quick fox'),"
                             "(4,'fox brown quick'),"
                             "(5,'lazy'),"
                             "(6,'New York'),"
                             "(7,'York New')"));
    m_db->updateSchema();

    m_model.reset(new SqliteTableModel(*m_db, nullptr, QString(), true));
    QSignalSpy rowCountSpy(m_model.get(), &SqliteTableModel::finishedRowCount);
    m_model->setQuery(sqlb::Query(sqlb::ObjectIdentifier("main", "t")));
    QVERIFY(rowCountSpy.wait(5000) || rowCountSpy.count() > 0);
    QCOMPARE(m_model->rowCount(), 7);
}

void TestFilterWords::cleanup()
{
    // The model has to go before the database it reads from
    m_model.reset();
    m_db.reset();
    m_dir.reset();
    setSplitWords(false);
}

QStringList TestFilterWords::allNames()
{
    if(!m_model->completeCache())
        QTest::qFail("the rows of the model could not be loaded", __FILE__, __LINE__);

    QStringList names;
    for(int row = 0; row < m_model->rowCount(); ++row)
        names << m_model->data(m_model->index(row, 2), Qt::EditRole).toString();
    return names;
}

QStringList TestFilterWords::filterNames(const QString& value)
{
    QSignalSpy rowCountSpy(m_model.get(), &SqliteTableModel::finishedRowCount);
    m_model->updateFilter("name", value);
    if(!(rowCountSpy.wait(5000) || rowCountSpy.count() > 0))
        QTest::qFail("the model did not finish counting the rows after setting the filter", __FILE__, __LINE__);
    return allNames();
}

void TestFilterWords::phraseIsMatchedByDefault()
{
    // Without the setting a filter with several words is a phrase, as it always was
    QCOMPARE(filterNames("quick fox"), QStringList() << "quick fox");
    QCOMPARE(filterNames("fox quick"), QStringList());
    QCOMPARE(filterNames("brown"), QStringList() << "the quick brown fox" << "brown dog" << "fox brown quick");
}

void TestFilterWords::wordsAreMatchedInAnyOrder_data()
{
    QTest::addColumn<QString>("filter");
    QTest::addColumn<QStringList>("expected");

    QTest::newRow("two words")
        << "quick fox" << (QStringList() << "the quick brown fox" << "quick fox" << "fox brown quick");
    QTest::newRow("two words swapped")
        << "fox quick" << (QStringList() << "the quick brown fox" << "quick fox" << "fox brown quick");
    QTest::newRow("three words")
        << "fox quick brown" << (QStringList() << "the quick brown fox" << "fox brown quick");
    QTest::newRow("several spaces")
        << "  quick    fox  " << (QStringList() << "the quick brown fox" << "quick fox" << "fox brown quick");
    QTest::newRow("words of a name in reverse")
        << "York New" << (QStringList() << "New York" << "York New");
    QTest::newRow("one word")
        << "brown" << (QStringList() << "the quick brown fox" << "brown dog" << "fox brown quick");
    QTest::newRow("case is ignored like in a normal filter")
        << "QUICK Fox" << (QStringList() << "the quick brown fox" << "quick fox" << "fox brown quick");
    QTest::newRow("no row has all the words")
        << "lazy fox" << QStringList();
}

void TestFilterWords::wordsAreMatchedInAnyOrder()
{
    QFETCH(QString, filter);
    QFETCH(QStringList, expected);

    setSplitWords(true);
    QCOMPARE(filterNames(filter), expected);
}

void TestFilterWords::specialFiltersAreNotSplit_data()
{
    QTest::addColumn<QString>("filter");
    QTest::addColumn<QStringList>("expected");

    // These must work exactly as without the setting, even if they contain spaces
    QTest::newRow("exact match")
        << "=quick fox" << (QStringList() << "quick fox");
    QTest::newRow("wildcards")
        << "%quick fox%" << (QStringList() << "quick fox");
    QTest::newRow("wildcard in the middle")
        << "the%fox" << (QStringList() << "the quick brown fox");
    QTest::newRow("unequal")
        << "<>quick fox" << (QStringList() << "the quick brown fox" << "brown dog" << "fox brown quick" << "lazy" << "New York" << "York New");
    QTest::newRow("greater than")
        << ">quick fox" << (QStringList() << "the quick brown fox");
    QTest::newRow("leading space is kept for a single word")
        << " fox" << (QStringList() << "the quick brown fox" << "quick fox");
    QTest::newRow("empty filter")
        << "" << (QStringList() << "the quick brown fox" << "brown dog" << "quick fox" << "fox brown quick" << "lazy" << "New York" << "York New");
}

void TestFilterWords::specialFiltersAreNotSplit()
{
    QFETCH(QString, filter);
    QFETCH(QStringList, expected);

    setSplitWords(true);
    QCOMPARE(filterNames(filter), expected);
}

void TestFilterWords::filtersOfOtherColumnsStay()
{
    setSplitWords(true);

    // Filter the id column, too. Both filters have to apply and the filters of two columns are counted as two filters.
    {
        QSignalSpy rowCountSpy(m_model.get(), &SqliteTableModel::finishedRowCount);
        m_model->updateFilter("id", ">1");
        QVERIFY(rowCountSpy.wait(5000) || rowCountSpy.count() > 0);
    }
    QCOMPARE(filterNames("fox quick"), QStringList() << "quick fox" << "fox brown quick");
    QCOMPARE(m_model->filterCount(), size_t(2));

    // Removing the filter of the name column removes all of its conditions but keeps the other filter
    QCOMPARE(filterNames(""), QStringList() << "brown dog" << "quick fox" << "fox brown quick" << "lazy" << "New York" << "York New");
    QCOMPARE(m_model->filterCount(), size_t(1));
}

void TestFilterWords::newFilterReplacesOldWords()
{
    setSplitWords(true);

    QCOMPARE(filterNames("quick fox"), QStringList() << "the quick brown fox" << "quick fox" << "fox brown quick");

    // Setting another filter for the column must not add to the old words
    QCOMPARE(filterNames("lazy"), QStringList() << "lazy");
    QCOMPARE(filterNames("york new"), QStringList() << "New York" << "York New");
    QCOMPARE(filterNames("brown dog"), QStringList() << "brown dog");
}

void TestFilterWords::conditionsOfWords_data()
{
    QTest::addColumn<QString>("filter");
    QTest::addColumn<bool>("split");
    QTest::addColumn<QStringList>("expected");

    QTest::newRow("phrase without the setting")
        << "red car" << false << (QStringList() << "LIKE '%red car%' ESCAPE '\\'");
    QTest::newRow("two words")
        << "red car" << true << (QStringList() << "LIKE '%red%' ESCAPE '\\'" << "LIKE '%car%' ESCAPE '\\'");
    QTest::newRow("three words and extra spaces")
        << " red  big car " << true << (QStringList() << "LIKE '%red%' ESCAPE '\\'" << "LIKE '%big%' ESCAPE '\\'" << "LIKE '%car%' ESCAPE '\\'");
    QTest::newRow("quote in a word")
        << "it's red" << true << (QStringList() << "LIKE '%it''s%' ESCAPE '\\'" << "LIKE '%red%' ESCAPE '\\'");
    QTest::newRow("one word")
        << "red" << true << (QStringList() << "LIKE '%red%' ESCAPE '\\'");
    QTest::newRow("one word with spaces around is left as it is")
        << " red " << true << (QStringList() << "LIKE '% red %' ESCAPE '\\'");
    QTest::newRow("only spaces are left as they are")
        << "   " << true << (QStringList() << "LIKE '%   %' ESCAPE '\\'");
    QTest::newRow("later word looks like an operator")
        << "bold <b>" << true << (QStringList() << "LIKE '%bold%' ESCAPE '\\'" << "LIKE '%<b>%' ESCAPE '\\'");
    QTest::newRow("later word looks like a regular expression")
        << "path /usr/" << true << (QStringList() << "LIKE '%path%' ESCAPE '\\'" << "LIKE '%/usr/%' ESCAPE '\\'");
    QTest::newRow("empty")
        << "" << true << QStringList();
    QTest::newRow("empty without the setting")
        << "" << false << QStringList();
}

void TestFilterWords::conditionsOfWords()
{
    QFETCH(QString, filter);
    QFETCH(bool, split);
    QFETCH(QStringList, expected);

    QCOMPARE(toStrings(CondFormat::filterToSqlConditions(filter, QString(), split)), expected);
}

void TestFilterWords::conditionsOfSpecialFilters_data()
{
    QTest::addColumn<QString>("filter");

    // None of these is split, even if the setting is on
    QTest::newRow("equal") << "=red car";
    QTest::newRow("equal to null") << "=NULL";
    QTest::newRow("not null") << "<>NULL";
    QTest::newRow("not empty") << "<>''";
    QTest::newRow("unequal") << "<>red car";
    QTest::newRow("greater") << ">red car";
    QTest::newRow("greater or equal") << ">=red car";
    QTest::newRow("less") << "<red car";
    QTest::newRow("less or equal") << "<=red car";
    QTest::newRow("range") << "1~5";
    QTest::newRow("regular expression") << "/red car/";
    QTest::newRow("regular expression with alternatives") << "/red|big car/";
    QTest::newRow("wildcard at the start") << "%red car";
    QTest::newRow("wildcard at the end") << "red car%";
    QTest::newRow("wildcard in the middle") << "red%car big";
    QTest::newRow("wildcards around") << "%red car%";
}

void TestFilterWords::conditionsOfSpecialFilters()
{
    QFETCH(QString, filter);

    const std::string single = CondFormat::filterToSqlCondition(filter);
    QVERIFY2(!single.empty(), "the filter should result in a condition");
    const std::vector<std::string> conditions = CondFormat::filterToSqlConditions(filter, QString(), true);
    QVERIFY2(conditions.size() == 1, qPrintable("the filter was split into " + QString::number(conditions.size()) + " conditions: " + toStrings(conditions).join(" | ")));
    QCOMPARE(QString::fromStdString(conditions.front()), QString::fromStdString(single));
}

void TestFilterWords::conditionsKeepNonAsciiWords()
{
    // Do not write non-ASCII characters as literals here. Not all compilers read them as UTF-8.
    const QString cafe = QString("caf") + QChar(0x00E9);
    const QString naive = QString("na") + QChar(0x00EF) + QString("ve");

    const QStringList expected = QStringList() << "LIKE '%" + cafe + "%' ESCAPE '\\'" << "LIKE '%" + naive + "%' ESCAPE '\\'";
    QCOMPARE(toStrings(CondFormat::filterToSqlConditions(cafe + " " + naive, QString(), true)), expected);
}

void TestFilterWords::queryRequiresAllConditionsOfColumn()
{
    // Several conditions for one column and another one for another column all have to be met by the rows
    sqlb::Query query(sqlb::ObjectIdentifier("main", "t"));
    query.setColumnNames({"id", "name"});
    for(const std::string& condition : CondFormat::filterToSqlConditions("fox quick", QString(), true))
        query.where().insert({"name", condition});
    query.where().insert({"id", "> 1"});
    QCOMPARE(query.where().size(), size_t(3));

    sqlite3* db = nullptr;
    QVERIFY(sqlite3_open(":memory:", &db) == SQLITE_OK);
    QStringList rows;
    const int result = sqlite3_exec(db,
        "CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT);"
        "INSERT INTO t VALUES(1,'the quick brown fox'),(2,'brown dog'),(3,'quick fox'),(4,'fox brown quick'),(5,'lazy');",
        nullptr, nullptr, nullptr);
    QVERIFY(result == SQLITE_OK);

    const std::string sql = query.buildQuery(false) + " ORDER BY id";
    const int queryResult = sqlite3_exec(db, sql.c_str(), [](void* data, int count, char** values, char**) {
        if(count > 1 && values[1])
            static_cast<QStringList*>(data)->push_back(QString::fromUtf8(values[1]));
        return 0;
    }, &rows, nullptr);
    sqlite3_close(db);
    QVERIFY2(queryResult == SQLITE_OK, qPrintable("the query failed: " + QString::fromStdString(sql)));
    QCOMPARE(rows, QStringList() << "quick fox" << "fox brown quick");
}
