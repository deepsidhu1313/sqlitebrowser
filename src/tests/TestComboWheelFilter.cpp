#include <QtTest/QTest>
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QFontComboBox>
#include <QLineEdit>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "TestComboWheelFilter.h"
#include "../ComboWheelFilter.h"

QTEST_MAIN(TestComboWheelFilter)

namespace {

const int WheelDown = -120;     // One notch of a mouse wheel, scrolling down

// Sends a wheel event to a widget, the same way the event loop would deliver it. Returns true if the widget
// has handled it and false if the widget has left it for its parent widget, e.g. a scroll area.
bool sendWheel(QWidget* widget, int delta = WheelDown)
{
    const QPointF pos = widget->rect().center();
    const QPointF globalPos = widget->mapToGlobal(widget->rect().center());
#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
    QWheelEvent event(pos, globalPos, QPoint(), QPoint(0, delta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
#else
    QWheelEvent event(pos.toPoint(), globalPos.toPoint(), delta, Qt::NoButton, Qt::NoModifier, Qt::Vertical);
#endif
    // Start out ignored like the events coming from the window system, so only a widget handling it can accept it
    event.ignore();
    QApplication::sendEvent(widget, &event);
    return event.isAccepted();
}

// A window with a line edit and a combo box. The line edit has the keyboard focus, i.e. the combo box doesn't.
struct Fixture
{
    Fixture()
    {
        QVBoxLayout* layout = new QVBoxLayout(&window);
        edit = new QLineEdit(&window);
        combo = new QComboBox(&window);
        combo->addItems({"one", "two", "three", "four"});
        layout->addWidget(edit);
        layout->addWidget(combo);
    }

    bool show()
    {
        window.show();
        if(!QTest::qWaitForWindowActive(&window))
            return false;
        edit->setFocus();
        return QTest::qWaitFor([this]() { return edit->hasFocus(); });
    }

    bool focus(QWidget* widget)
    {
        widget->setFocus();
        return QTest::qWaitFor([this]() { return combo->hasFocus(); });
    }

    QWidget window;
    QLineEdit* edit;
    QComboBox* combo;
};

QByteArray state(const QComboBox* combo, bool handled)
{
    return "combo index " + QByteArray::number(combo->currentIndex()) + ", wheel event " + (handled ? "handled" : "left to the parent widget");
}

}

void TestComboWheelFilter::init()
{
    m_filter = new ComboWheelFilter(qApp);
    qApp->installEventFilter(m_filter);
}

void TestComboWheelFilter::cleanup()
{
    qApp->removeEventFilter(m_filter);
    delete m_filter;
}

void TestComboWheelFilter::inactiveFilterKeepsStandardBehaviour()
{
    Fixture f;
    QVERIFY(f.show());
    QVERIFY(!m_filter->isActive());
    QVERIFY(!f.combo->hasFocus());

    const bool handled = sendWheel(f.combo);

    // Without the filter being activated the wheel changes the combo box, even though it doesn't have the focus
    QVERIFY2(f.combo->currentIndex() == 1 && handled, state(f.combo, handled).constData());
    QCOMPARE(f.combo->focusPolicy(), Qt::WheelFocus);
}

void TestComboWheelFilter::unfocusedComboIgnoresWheel()
{
    Fixture f;
    QVERIFY(f.show());
    m_filter->setActive(true);
    QVERIFY(!f.combo->hasFocus());

    // The event must be passed on instead of being swallowed, otherwise a scroll area could not use it
    const bool handled = sendWheel(f.combo);

    QVERIFY2(f.combo->currentIndex() == 0 && !handled, state(f.combo, handled).constData());
}

void TestComboWheelFilter::focusedComboHandlesWheel()
{
    Fixture f;
    QVERIFY(f.show());
    m_filter->setActive(true);
    QVERIFY(f.focus(f.combo));

    bool handled = sendWheel(f.combo);
    QVERIFY2(f.combo->currentIndex() == 1 && handled, state(f.combo, handled).constData());

    // And back up again
    handled = sendWheel(f.combo, -WheelDown);
    QVERIFY2(f.combo->currentIndex() == 0 && handled, state(f.combo, handled).constData());
}

void TestComboWheelFilter::editableComboFollowsFocusOfItsLineEdit()
{
    Fixture f;
    f.combo->setEditable(true);
    QVERIFY(f.show());
    m_filter->setActive(true);
    QVERIFY(!f.combo->hasFocus());

    bool handled = sendWheel(f.combo);
    QVERIFY2(f.combo->currentIndex() == 0 && !handled, state(f.combo, handled).constData());

    // The keyboard focus of an editable combo box is in its line edit
    QVERIFY(f.focus(f.combo->lineEdit()));

    handled = sendWheel(f.combo);
    QVERIFY2(f.combo->currentIndex() == 1 && handled, state(f.combo, handled).constData());
}

void TestComboWheelFilter::fontComboIsCovered()
{
    // The font selection in the preferences is a QFontComboBox
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    QLineEdit* edit = new QLineEdit(&window);
    QFontComboBox* fonts = new QFontComboBox(&window);
    layout->addWidget(edit);
    layout->addWidget(fonts);
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    edit->setFocus();
    QVERIFY(QTest::qWaitFor([edit]() { return edit->hasFocus(); }));
    if(fonts->count() < 2)
        QSKIP("This test needs at least two fonts to be installed");
    m_filter->setActive(true);
    const int index = fonts->currentIndex();

    const bool handled = sendWheel(fonts);

    QVERIFY2(fonts->currentIndex() == index && !handled,
             ("font index " + QByteArray::number(fonts->currentIndex()) + " instead of " + QByteArray::number(index) +
              ", wheel event " + (handled ? "handled" : "left to the parent widget")).constData());
}

void TestComboWheelFilter::wheelFocusIsDisabledWhileActive()
{
    // A combo box with the wheel focus policy takes the focus when the wheel is turned over it, before the wheel
    // event is even delivered. Then it would always have the focus when the filter looks at it.
    Fixture f;
    QVERIFY(f.show());
    QCOMPARE(f.combo->focusPolicy(), Qt::WheelFocus);

    m_filter->setActive(true);

    QVERIFY2(f.combo->focusPolicy() != Qt::WheelFocus, "the combo box still takes the focus from the mouse wheel");
    // But it can still be reached with the mouse and the keyboard
    QVERIFY2(f.combo->focusPolicy() & Qt::ClickFocus, "the combo box can't be focused by clicking it any more");
    QVERIFY2(f.combo->focusPolicy() & Qt::TabFocus, "the combo box can't be focused with the tab key any more");
}

void TestComboWheelFilter::comboBoxesCreatedWhileActiveAreCovered()
{
    Fixture f;
    QVERIFY(f.show());
    m_filter->setActive(true);

    // Like the cell widgets of the table definition, which are created on demand
    QComboBox* late = new QComboBox(&f.window);
    late->addItems({"one", "two", "three"});
    f.window.layout()->addWidget(late);
    late->show();
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    QVERIFY(!late->hasFocus());

    QVERIFY2(late->focusPolicy() != Qt::WheelFocus, "the new combo box still takes the focus from the mouse wheel");
    const bool handled = sendWheel(late);
    QVERIFY2(late->currentIndex() == 0 && !handled, state(late, handled).constData());
}

void TestComboWheelFilter::deactivatingRestoresStandardBehaviour()
{
    Fixture f;
    QVERIFY(f.show());
    m_filter->setActive(true);
    m_filter->setActive(false);

    QCOMPARE(f.combo->focusPolicy(), Qt::WheelFocus);
    const bool handled = sendWheel(f.combo);
    QVERIFY2(f.combo->currentIndex() == 1 && handled, state(f.combo, handled).constData());
}

void TestComboWheelFilter::popupListIsNotAffected()
{
    Fixture f;
    for(int i = 0; i < 100; ++i)
        f.combo->addItem(QString("item %1").arg(i));
    QVERIFY(f.show());
    m_filter->setActive(true);

    f.combo->showPopup();
    QAbstractItemView* list = f.combo->view();
    QVERIFY(QTest::qWaitFor([list]() { return list->isVisible(); }));
    QVERIFY2(list->verticalScrollBar()->maximum() > 0, "the list has nothing to scroll");

    // The wheel works in the popup list although the combo box itself doesn't have the focus
    sendWheel(list->viewport());
    const int listPosition = list->verticalScrollBar()->value();
    f.combo->hidePopup();

    QVERIFY2(listPosition > 0, ("list position " + QByteArray::number(listPosition)).constData());
    QCOMPARE(f.combo->currentIndex(), 0);
}
