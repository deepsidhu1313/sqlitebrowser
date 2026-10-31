#ifndef TESTSQLEDITOR_H
#define TESTSQLEDITOR_H

#include <QObject>
#include <QTemporaryDir>

class SqlTextEdit;

class TestSqlEditor : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void zoomKeys_data();
    void zoomKeys();
    void ctrlZeroStaysWithTheApplication();

private:
    QTemporaryDir settingsDir;

    // The static SQL lexer is owned by the first SqlTextEdit that is created, so this one must outlive all the others
    SqlTextEdit* lexerOwner;
};

#endif // TESTSQLEDITOR_H
