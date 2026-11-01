#ifndef TESTBUSYTIMEOUT_H
#define TESTBUSYTIMEOUT_H

#include <QObject>

class TestBusyTimeout : public QObject
{
    Q_OBJECT

private slots:
    void defaultIsNoTimeout();
    void timeoutIsAppliedOnOpen_data();
    void timeoutIsAppliedOnOpen();
    void timeoutIsAppliedOnCreate();
    void lockedDatabaseFailsImmediatelyByDefault();
    void lockedDatabaseIsWaitedForWithTimeout();
};

#endif // TESTBUSYTIMEOUT_H
