#ifndef TESTBSON_H
#define TESTBSON_H

#include <QObject>

class TestBson : public QObject
{
    Q_OBJECT

private slots:
    void decodesToExtendedJson_data();
    void decodesToExtendedJson();
    void decodesDoubles_data();
    void decodesDoubles();
    void decodesNonAsciiText();
    void decodesEmbeddedNulInStrings();
    void rejectsInvalidData_data();
    void rejectsInvalidData();
    void rejectsOtherFormats_data();
    void rejectsOtherFormats();
    void leavesOutputAloneOnFailure();
    void limitsNestingDepth_data();
    void limitsNestingDepth();
    void limitsSize();
};

#endif // TESTBSON_H
