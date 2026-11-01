#ifndef TESTTYPECOLOURS_H
#define TESTTYPECOLOURS_H

#include <QObject>

#include <memory>

class QColor;
class QTemporaryDir;
class DBBrowserDB;
class SqliteTableModel;

class TestTypeColours : public QObject
{
    Q_OBJECT

public:
    TestTypeColours();
    ~TestTypeColours() override;

private slots:
    // This one has to be the first one because it looks at the default values of the settings
    void defaultColoursAreValidAndDifferent();

    void init();
    void cleanup();

    void typesAreNotColouredByDefault_data();
    void typesAreNotColouredByDefault();
    void cellsGetTheColourOfTheirType_data();
    void cellsGetTheColourOfTheirType();
    void backgroundIsNotChanged();
    void conditionalFormatWins();
    void changedColoursAreUsedAfterReloading();

private:
    // Set the colours which are used by the tests and whether the colour by type is enabled. Then create the model.
    void createModel(bool colourByType);
    QColor foreground(int row, int column) const;

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<DBBrowserDB> m_db;
    std::unique_ptr<SqliteTableModel> m_model;
};

#endif // TESTTYPECOLOURS_H
