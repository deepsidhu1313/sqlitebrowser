#ifndef TESTDBCONFIG_H
#define TESTDBCONFIG_H

#include <QObject>

class TestDbConfig : public QObject
{
    Q_OBJECT

private slots:
    void noDatabaseOpen();
    void toggleOptions_data();
    void toggleOptions();
    void doubleQuotedStringsInDml();
    void doubleQuotedStringsInDdl();
    void defensiveMode();
    void triggersCanBeDisabled();
    void optionsAreNotStoredInTheFile();
};

#endif // TESTDBCONFIG_H
