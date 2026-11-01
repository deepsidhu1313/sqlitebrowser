#include <QtTest/QTest>
#include <QApplication>
#include <QStandardItemModel>
#include <QTableView>

#include "TestFilterNavigation.h"
#include "../FilterLineEdit.h"
#include "../FilterTableHeader.h"

QTEST_MAIN(TestFilterNavigation)

namespace {

// A table with a filter row like the one of the Browse Data tab
struct Fixture
{
    explicit Fixture(int rows = 5, int columns = 3) :
        model(rows, columns),
        header(new FilterTableHeader(&table))
    {
        for(int r = 0; r < rows; ++r)
            for(int c = 0; c < columns; ++c)
                model.setItem(r, c, new QStandardItem(QString("r%1c%2").arg(r).arg(c)));
        table.setHorizontalHeader(header);
        table.setModel(&model);
        header->generateFilters(static_cast<size_t>(columns), 0);
        table.resize(500, 200);
    }

    bool show()
    {
        table.show();
        return QTest::qWaitForWindowActive(&table);
    }

    FilterLineEdit* filter(int column) const
    {
        const auto filters = header->findChildren<FilterLineEdit*>();
        for(FilterLineEdit* f : filters)
            if(f->property("column").toInt() == column)
                return f;
        return nullptr;
    }

    // Puts the keyboard focus into a filter, like the user clicking it
    bool focusFilter(int column)
    {
        FilterLineEdit* f = filter(column);
        f->setFocus();
        return QTest::qWaitFor([f]() { return f->hasFocus(); });
    }

    QByteArray state() const
    {
        const QWidget* focus = QApplication::focusWidget();
        const QModelIndex current = table.currentIndex();
        return QByteArray("focus is ") + (focus == &table ? "the table" : (focus ? focus->metaObject()->className() : "nothing")) +
                ", current cell is " + QByteArray::number(current.row()) + "/" + QByteArray::number(current.column()) +
                ", " + QByteArray::number(table.selectionModel()->selectedIndexes().size()) + " cells are selected";
    }

    QStandardItemModel model;
    QTableView table;
    FilterTableHeader* header;
};

}

void TestFilterNavigation::downMovesFocusToGrid()
{
    Fixture f;
    QVERIFY(f.show());
    QVERIFY(f.focusFilter(1));

    QTest::keyClick(f.filter(1), Qt::Key_Down);

    // The cell below the filter is selected as a whole and the grid has the focus, so the arrow keys move around in it
    QVERIFY2(f.table.hasFocus(), f.state().constData());
    QVERIFY2(f.table.currentIndex() == f.model.index(0, 1), f.state().constData());
    QVERIFY2(f.table.selectionModel()->selectedIndexes() == QModelIndexList{f.model.index(0, 1)}, f.state().constData());

    QTest::keyClick(&f.table, Qt::Key_Down);
    QVERIFY2(f.table.currentIndex() == f.model.index(1, 1), f.state().constData());
}

void TestFilterNavigation::downKeepsCurrentRow()
{
    Fixture f;
    QVERIFY(f.show());
    f.table.setCurrentIndex(f.model.index(3, 0));
    QVERIFY(f.focusFilter(2));

    QTest::keyClick(f.filter(2), Qt::Key_Down);

    // Back to the row the user was in, just in the column of the filter
    QVERIFY2(f.table.hasFocus() && f.table.currentIndex() == f.model.index(3, 2), f.state().constData());
}

void TestFilterNavigation::downGoesToFirstVisibleRowWhenScrolled()
{
    Fixture f(200);
    QVERIFY(f.show());
    f.table.scrollTo(f.model.index(150, 0), QAbstractItemView::PositionAtTop);
    const int firstVisible = f.table.rowAt(0);
    QVERIFY2(firstVisible > 100, "the table did not scroll");
    QVERIFY(f.focusFilter(0));

    QTest::keyClick(f.filter(0), Qt::Key_Down);

    QVERIFY2(f.table.hasFocus() && f.table.currentIndex() == f.model.index(firstVisible, 0), f.state().constData());
}

void TestFilterNavigation::downDoesNothingWithoutRows()
{
    Fixture f(0);
    QVERIFY(f.show());
    QVERIFY(f.focusFilter(1));

    QTest::keyClick(f.filter(1), Qt::Key_Down);

    QVERIFY2(f.filter(1)->hasFocus() && !f.table.currentIndex().isValid(), f.state().constData());
}

void TestFilterNavigation::downWithModifiersIsLeftToTheLineEdit()
{
    Fixture f;
    QVERIFY(f.show());
    QVERIFY(f.focusFilter(1));

    QTest::keyClick(f.filter(1), Qt::Key_Down, Qt::ShiftModifier);

    QVERIFY2(f.filter(1)->hasFocus(), f.state().constData());
}

void TestFilterNavigation::downKeepsFilterText()
{
    Fixture f;
    QVERIFY(f.show());
    QVERIFY(f.focusFilter(1));
    QTest::keyClicks(f.filter(1), "c1");
    QCOMPARE(f.filter(1)->text(), QString("c1"));

    QTest::keyClick(f.filter(1), Qt::Key_Down);

    QVERIFY2(f.table.hasFocus(), f.state().constData());
    QCOMPARE(f.filter(1)->text(), QString("c1"));
}

void TestFilterNavigation::otherKeysStayInTheFilter()
{
    Fixture f;
    QVERIFY(f.show());
    QVERIFY(f.focusFilter(1));
    QTest::keyClicks(f.filter(1), "abc");

    // Editing the text must keep working
    QTest::keyClick(f.filter(1), Qt::Key_Left);
    QTest::keyClick(f.filter(1), Qt::Key_Backspace);
    QTest::keyClick(f.filter(1), Qt::Key_Right);
    QTest::keyClick(f.filter(1), Qt::Key_Up);

    QVERIFY2(f.filter(1)->hasFocus(), f.state().constData());
    QCOMPARE(f.filter(1)->text(), QString("ac"));
}

void TestFilterNavigation::tabStillMovesToNextFilter()
{
    Fixture f;
    QVERIFY(f.show());
    QVERIFY(f.focusFilter(1));

    QTest::keyClick(f.filter(1), Qt::Key_Tab);

    QVERIFY2(f.filter(2)->hasFocus(), f.state().constData());
}
