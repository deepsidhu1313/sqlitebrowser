#ifndef TESTFILTERWORDS_H
#define TESTFILTERWORDS_H

#include <QObject>

#include <memory>

class QTemporaryDir;
class DBBrowserDB;
class SqliteTableModel;

class TestFilterWords : public QObject
{
    Q_OBJECT

public:
    TestFilterWords();
    ~TestFilterWords() override;

private slots:
    void init();
    void cleanup();

    void phraseIsMatchedByDefault();
    void wordsAreMatchedInAnyOrder_data();
    void wordsAreMatchedInAnyOrder();
    void specialFiltersAreNotSplit_data();
    void specialFiltersAreNotSplit();
    void filtersOfOtherColumnsStay();
    void newFilterReplacesOldWords();

    void conditionsOfWords_data();
    void conditionsOfWords();
    void conditionsOfSpecialFilters_data();
    void conditionsOfSpecialFilters();
    void conditionsKeepNonAsciiWords();
    void queryRequiresAllConditionsOfColumn();

private:
    // Sets the filter of the name column and returns the names of the rows which are left, ordered by their id
    QStringList filterNames(const QString& value);
    QStringList allNames();

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<DBBrowserDB> m_db;
    std::unique_ptr<SqliteTableModel> m_model;
};

#endif // TESTFILTERWORDS_H
