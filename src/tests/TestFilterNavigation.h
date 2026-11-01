#ifndef TESTFILTERNAVIGATION_H
#define TESTFILTERNAVIGATION_H

#include <QObject>

class TestFilterNavigation : public QObject
{
    Q_OBJECT

private slots:
    void downMovesFocusToGrid();
    void downKeepsCurrentRow();
    void downGoesToFirstVisibleRowWhenScrolled();
    void downDoesNothingWithoutRows();
    void downWithModifiersIsLeftToTheLineEdit();
    void downKeepsFilterText();
    void otherKeysStayInTheFilter();
    void tabStillMovesToNextFilter();
};

#endif
