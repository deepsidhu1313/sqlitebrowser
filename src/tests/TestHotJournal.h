#ifndef TESTHOTJOURNAL_H
#define TESTHOTJOURNAL_H

#include <QObject>

class TestHotJournal : public QObject
{
    Q_OBJECT

private slots:
    void openRecoversFromHotJournal();
    void openReadOnlyExplainsHotJournal();
    void emptyLeftoverJournalIsHarmless();
};

#endif
