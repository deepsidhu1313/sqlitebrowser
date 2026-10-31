#include "TestSqlEditor.h"

#include "../Settings.h"
#include "../sqltextedit.h"

#include <QApplication>
#include <QKeyEvent>
#include <QtTest/QtTest>

QTEST_MAIN(TestSqlEditor)

namespace
{

// Zoom level of the editor in points relative to its normal font size
int zoomLevel(SqlTextEdit& edit)
{
    return static_cast<int>(edit.SendScintilla(QsciScintillaBase::SCI_GETZOOM));
}

// Shortcuts constrained to the widget only fire when the editor has the focus in the active window
bool showWithFocus(SqlTextEdit& edit)
{
    edit.resize(400, 300);
    edit.show();
    if(!QTest::qWaitForWindowActive(&edit))
        return false;
    edit.setFocus();
    return edit.hasFocus();
}

}

void TestSqlEditor::initTestCase()
{
    // Never read or write the settings of the real user
    QVERIFY(settingsDir.isValid());
    Settings::setUserSettingsFile(settingsDir.filePath("settings.ini"));

    lexerOwner = new SqlTextEdit();
}

void TestSqlEditor::cleanupTestCase()
{
    delete lexerOwner;
    lexerOwner = nullptr;
}

void TestSqlEditor::zoomKeys_data()
{
    QTest::addColumn<int>("key");
    QTest::addColumn<int>("modifiers");
    QTest::addColumn<int>("startZoom");
    QTest::addColumn<int>("expectedZoom");

    const int ctrl = Qt::ControlModifier;
    const int shift = Qt::ShiftModifier;
    const int keypad = Qt::KeypadModifier;

    // The bindings QScintilla provides
    QTest::newRow("Ctrl++ zooms in")            << int(Qt::Key_Plus)  << ctrl                              << 0 << 1;
    QTest::newRow("Ctrl+- zooms out")           << int(Qt::Key_Minus) << ctrl                              << 0 << -1;
    QTest::newRow("Ctrl+keypad+ zooms in")      << int(Qt::Key_Plus)  << (ctrl | keypad)       << 0 << 1;
    QTest::newRow("Ctrl+keypad- zooms out")     << int(Qt::Key_Minus) << (ctrl | keypad)       << 0 << -1;

    // On keyboards where + needs Shift (US: Shift+=, Japanese: Shift+;) the plain Ctrl++ never arrives
    QTest::newRow("Ctrl+Shift++ zooms in")      << int(Qt::Key_Plus)  << (ctrl | shift)                    << 0 << 1;
    QTest::newRow("Ctrl+= zooms in")            << int(Qt::Key_Equal) << ctrl                              << 0 << 1;

    // Back to the normal size
    QTest::newRow("Ctrl+Shift+0 resets from bigger")  << int(Qt::Key_0) << (ctrl | shift) << 4  << 0;
    QTest::newRow("Ctrl+Shift+0 resets from smaller") << int(Qt::Key_0) << (ctrl | shift) << -3 << 0;
    QTest::newRow("Ctrl+Shift+0 keeps the normal size") << int(Qt::Key_0) << (ctrl | shift) << 0 << 0;
}

void TestSqlEditor::zoomKeys()
{
    QFETCH(int, key);
    QFETCH(int, modifiers);
    QFETCH(int, startZoom);
    QFETCH(int, expectedZoom);

    SqlTextEdit edit;
    QVERIFY(showWithFocus(edit));
    edit.zoomTo(startZoom);
    QCOMPARE(zoomLevel(edit), startZoom);

    QTest::keyClick(&edit, static_cast<Qt::Key>(key), static_cast<Qt::KeyboardModifiers>(modifiers));

    QCOMPARE(zoomLevel(edit), expectedZoom);
}

void TestSqlEditor::ctrlZeroStaysWithTheApplication()
{
    // Ctrl+0 is the shortcut of View > Window Layout > Reset Window Layout in the main window.
    // The editor must not claim it, otherwise that shortcut would stop working while typing SQL.
    SqlTextEdit edit;
    QVERIFY(showWithFocus(edit));

    QKeyEvent override(QEvent::ShortcutOverride, Qt::Key_0, Qt::ControlModifier);
    override.ignore();
    QApplication::sendEvent(&edit, &override);
    QVERIFY(!override.isAccepted());

    edit.zoomTo(3);
    QTest::keyClick(&edit, Qt::Key_0, Qt::ControlModifier);
    QCOMPARE(zoomLevel(edit), 3);
}
