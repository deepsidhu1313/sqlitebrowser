#ifndef TESTCOMBOWHEELFILTER_H
#define TESTCOMBOWHEELFILTER_H

#include <QObject>

class ComboWheelFilter;

class TestComboWheelFilter : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void inactiveFilterKeepsStandardBehaviour();
    void unfocusedComboIgnoresWheel();
    void focusedComboHandlesWheel();
    void editableComboFollowsFocusOfItsLineEdit();
    void fontComboIsCovered();
    void wheelFocusIsDisabledWhileActive();
    void comboBoxesCreatedWhileActiveAreCovered();
    void deactivatingRestoresStandardBehaviour();
    void popupListIsNotAffected();

private:
    ComboWheelFilter* m_filter;
};

#endif
