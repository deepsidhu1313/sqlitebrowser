#include "TestBson.h"

#include "../BsonUtil.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

#include <cstring>

QTEST_APPLESS_MAIN(TestBson)

namespace
{

QByteArray int32le(qint32 value)
{
    QByteArray result;
    for(int i=0;i<4;++i)
        result.append(static_cast<char>((static_cast<quint32>(value) >> (8*i)) & 0xFF));
    return result;
}

// A BSON document with the given elements
QByteArray document(const QByteArray& elements)
{
    return int32le(elements.size() + 5) + elements + QByteArray(1, '\0');
}

// A single element: type, name and the encoded value
QByteArray element(char type, const char* name, const QByteArray& value)
{
    return QByteArray(1, type) + name + QByteArray(1, '\0') + value;
}

QByteArray stringValue(const QByteArray& utf8)
{
    return int32le(utf8.size() + 1) + utf8 + QByteArray(1, '\0');
}

QByteArray doubleValue(double value)
{
    quint64 bits;
    std::memcpy(&bits, &value, sizeof(bits));
    QByteArray result;
    for(int i=0;i<8;++i)
        result.append(static_cast<char>((bits >> (8*i)) & 0xFF));
    return result;
}

QByteArray allBytes()
{
    QByteArray all;
    for(int i=0;i<256;++i)
        all.append(static_cast<char>(i));
    return all;
}

// A document that contains 'levels' documents in total, i.e. the document itself and levels-1 documents nested in each other:
// {"d":{"d":{ ... {"n":null} ... }}}. This is built from the outside in so that it is cheap even for a huge number of levels.
QByteArray nestedDocument(int levels)
{
    // Every level adds 8 bytes: the length, the type and name of the element ('d' plus its terminator), and the document's terminator
    QByteArray result;
    result.reserve(levels * 9);
    for(int level=levels; level>=2; --level)
        result.append(int32le(8 * level)).append('\x03').append('d').append('\0');
    result.append(document(element('\x0A', "n", QByteArray())));
    result.append(QByteArray(levels - 1, '\0'));
    return result;
}

}

void TestBson::decodesToExtendedJson_data()
{
    QTest::addColumn<QString>("hex");
    QTest::addColumn<QString>("expected");

    QTest::newRow("string")
        << "160000000268656c6c6f0006000000776f726c640000"
        << R"json({"hello":"world"})json";
    QTest::newRow("bsonspec example")
        << "310000000442534f4e002600000002300008000000617765736f6d65000131003333333333331440103200c20700000000"
        << R"json({"BSON":["awesome",5.05,1986]})json";
    QTest::newRow("empty string and empty containers")
        << "1d00000002730001000000000364000500000000046100050000000000"
        << R"json({"s":"","d":{},"a":[]})json";
    QTest::newRow("null and booleans")
        << "100000000a6e00087400010866000000"
        << R"json({"n":null,"t":true,"f":false})json";
    QTest::newRow("nested documents")
        << "1c000000036100140000000362000c00000010630007000000000000"
        << R"json({"a":{"b":{"c":7}}})json";
    QTest::newRow("array of mixed values")
        << "2f00000004610027000000103000010000000231000200000078000a32000433000c00000010300002000000000000"
        << R"json({"a":[1,"x",null,[2]]})json";
    QTest::newRow("duplicate keys are kept")
        << "13000000106100010000001061000200000000"
        << R"json({"a":1,"a":2})json";
    QTest::newRow("array keys are not checked")
        << "1b0000000461001300000010350001000000107800020000000000"
        << R"json({"a":[1,2]})json";
    QTest::newRow("keep key order")
        << "1a000000107a000100000010610002000000106d000300000000"
        << R"json({"z":1,"a":2,"m":3})json";
    QTest::newRow("int32 limits")
        << "17000000106d696e0000000080106d617800ffffff7f00"
        << R"json({"min":-2147483648,"max":2147483647})json";
    QTest::newRow("int64 limits")
        << "1f000000126d696e000000000000000080126d617800ffffffffffffff7f00"
        << R"json({"min":-9223372036854775808,"max":9223372036854775807})json";
    QTest::newRow("doubles")
        << "47000000016100000000000000f03f016200000000000000e0bf0163000000000000005940016400343333333333d33f016500000000000000000001660000008054346f9d4100"
        << R"json({"a":1.0,"b":-0.5,"c":100.0,"d":0.30000000000000004,"e":0.0,"f":123456789.125})json";
    QTest::newRow("non finite doubles")
        << "2d000000016e616e00000000000000f87f01696e6600000000000000f07f016e696e6600000000000000f0ff00"
        << R"json({"nan":{"$numberDouble":"NaN"},"inf":{"$numberDouble":"Infinity"},"ninf":{"$numberDouble":"-Infinity"}})json";
    QTest::newRow("binary")
        << "30000000056200030000000001020305650000000000800575001000000004000102030405060708090a0b0c0d0e0f00"
        << R"json({"b":{"$binary":{"base64":"AQID","subType":"00"}},"e":{"$binary":{"base64":"","subType":"80"}},"u":{"$binary":{"base64":"AAECAwQFBgcICQoLDA0ODw==","subType":"04"}}})json";
    QTest::newRow("old binary subtype")
        << "1400000005620007000000020300000001020300"
        << R"json({"b":{"$binary":{"base64":"AQID","subType":"02"}}})json";
    QTest::newRow("object id")
        << "16000000075f69640056e1fc72e0c917e9c471416100"
        << R"json({"_id":{"$oid":"56e1fc72e0c917e9c4714161"}})json";
    QTest::newRow("dates")
        << "390000000965706f6368000000000000000000096d7300c4d8d6cc3b010000097300d0d6d6cc3b010000096c61737400ffdb1fd277e6000000"
        << R"json({"epoch":{"$date":"1970-01-01T00:00:00Z"},"ms":{"$date":"2012-12-24T12:15:30.500Z"},"s":{"$date":"2012-12-24T12:15:30Z"},"last":{"$date":"9999-12-31T23:59:59.999Z"}})json";
    QTest::newRow("dates outside of the ISO range")
        << "31000000096265666f726500ffffffffffffffff0961667465720000dc1fd277e60000096d696e00000000000000008000"
        << R"json({"before":{"$date":{"$numberLong":"-1"}},"after":{"$date":{"$numberLong":"253402300800000"}},"min":{"$date":{"$numberLong":"-9223372036854775808"}}})json";
    QTest::newRow("leap day")
        << "10000000096400000e3a9ddd00000000"
        << R"json({"d":{"$date":"2000-02-29T12:00:00Z"}})json";
    QTest::newRow("regular expression")
        << "120000000b72005e612e2a622400696d0000"
        << R"json({"r":{"$regularExpression":{"pattern":"^a.*b$","options":"im"}}})json";
    QTest::newRow("timestamp")
        << "1100000011747300020000000100000000"
        << R"json({"ts":{"$timestamp":{"t":1,"i":2}}})json";
    QTest::newRow("timestamp unsigned")
        << "1100000011747300ffffffffffffffff00"
        << R"json({"ts":{"$timestamp":{"t":4294967295,"i":4294967295}}})json";
    QTest::newRow("javascript")
        << "120000000d63000600000078203d20310000"
        << R"json({"c":{"$code":"x = 1"}})json";
    QTest::newRow("javascript with scope")
        << "200000000f63001800000004000000662829000c000000107800010000000000"
        << R"json({"c":{"$code":"f()","$scope":{"x":1}}})json";
    QTest::newRow("symbol")
        << "100000000e73000400000073796d0000"
        << R"json({"s":{"$symbol":"sym"}})json";
    QTest::newRow("db pointer")
        << "200000000c70000800000064622e636f6c6c0056e1fc72e0c917e9c471416100"
        << R"json({"p":{"$dbPointer":{"$ref":"db.coll","$id":{"$oid":"56e1fc72e0c917e9c4714161"}}}})json";
    QTest::newRow("undefined, min key and max key")
        << "10000000067500ff6c6f007f68690000"
        << R"json({"u":{"$undefined":true},"lo":{"$minKey":1},"hi":{"$maxKey":1}})json";
    QTest::newRow("decimal128")
        << "e20000001364300000000000000000000000000000004030136431000100000000000000000000000000403013643200010000000000000000000000000040b0136433000a000000000000000000000000003e3013643400010000000000000000000000000034301364350001000000000000000000000000003230136436007b000000000000000000000000002e301364370001000000000000000000000000004630136438000c00000000000000000000000000463013643900000000000000000000000000000000001364313000ffffffff638e8d37c087adbe09edff5f00"
        << R"json({"d0":{"$numberDecimal":"0"},"d1":{"$numberDecimal":"1"},"d2":{"$numberDecimal":"-1"},"d3":{"$numberDecimal":"1.0"},"d4":{"$numberDecimal":"0.000001"},"d5":{"$numberDecimal":"1E-7"},"d6":{"$numberDecimal":"1.23E-7"},"d7":{"$numberDecimal":"1E+3"},"d8":{"$numberDecimal":"1.2E+4"},"d9":{"$numberDecimal":"0E-6176"},"d10":{"$numberDecimal":"9.999999999999999999999999999999999E+6144"}})json";
    QTest::newRow("decimal128 specials")
        << "4500000013696e660000000000000000000000000000000078136e696e6600000000000000000000000000000000f8136e616e000000000000000000000000000000007c00"
        << R"json({"inf":{"$numberDecimal":"Infinity"},"ninf":{"$numberDecimal":"-Infinity"},"nan":{"$numberDecimal":"NaN"}})json";
    QTest::newRow("string escapes")
        << "180000000271226b000a0000006122625c630a09012f0000"
        << R"json({"q\"k":"a\"b\\c\n\t\u0001/"})json";
}

void TestBson::decodesToExtendedJson()
{
    QFETCH(QString, hex);
    QFETCH(QString, expected);

    const QByteArray data = QByteArray::fromHex(hex.toLatin1());
    QString text;
    QVERIFY2(bsonToText(data, text), qPrintable("a valid BSON document was not recognized: " + hex));
    QCOMPARE(text, expected);

    // Whatever comes out has to be valid JSON
    QJsonParseError error;
    QJsonDocument::fromJson(text.toUtf8(), &error);
    QVERIFY2(error.error == QJsonParseError::NoError, qPrintable("the result is not valid JSON (" + error.errorString() + "): " + text));
}

void TestBson::decodesDoubles_data()
{
    QTest::addColumn<double>("value");

    QTest::newRow("tiny") << 1e-7;
    QTest::newRow("smallest denormal") << 4.9406564584124654e-324;
    QTest::newRow("huge") << 1e300;
    QTest::newRow("largest") << 1.7976931348623157e308;
    QTest::newRow("negative") << -123456.789e10;
    QTest::newRow("needs 17 digits") << 1.0 / 3.0;
    QTest::newRow("negative zero") << -0.0;
}

void TestBson::decodesDoubles()
{
    QFETCH(double, value);

    QString text;
    QVERIFY(bsonToText(document(element('\x01', "v", doubleValue(value))), text));

    // The value has to survive the trip through the text exactly
    QJsonParseError error;
    const QJsonDocument json = QJsonDocument::fromJson(text.toUtf8(), &error);
    QVERIFY2(error.error == QJsonParseError::NoError, qPrintable("the result is not valid JSON (" + error.errorString() + "): " + text));
    const double decoded = json.object().value("v").toDouble();
    QVERIFY2(decoded == value, qPrintable(QString("%1 was decoded as %2 in %3").arg(value, 0, 'g', 17).arg(decoded, 0, 'g', 17).arg(text)));
    // And it is still recognizable as a floating point number
    QVERIFY2(text.contains('.') || text.contains('e'), qPrintable("looks like an integer: " + text));
}

void TestBson::decodesNonAsciiText()
{
    // e with an acute accent (U+00E9), the euro sign (U+20AC) and an emoji outside of the basic multilingual plane (U+1F600), as UTF-8
    const QByteArray utf8 = QByteArray::fromHex("c3a9e282acf09f9880");
    const QString expected = QString(QChar(0xE9)) + QChar(0x20AC) + QChar(QChar::highSurrogate(0x1F600)) + QChar(QChar::lowSurrogate(0x1F600));

    QString text;
    QVERIFY(bsonToText(document(element('\x02', "k", stringValue(utf8))), text));
    QCOMPARE(text, QString("{\"k\":\"") + expected + "\"}");

    // The same goes for names
    QVERIFY(bsonToText(document(element('\x0A', utf8.constData(), QByteArray())), text));
    QCOMPARE(text, QString("{\"") + expected + "\":null}");
}

void TestBson::decodesEmbeddedNulInStrings()
{
    QString text;
    QVERIFY(bsonToText(document(element('\x02', "k", stringValue(QByteArray("a\0b", 3)))), text));
    QCOMPARE(text, QString("{\"k\":\"a\\u0000b\"}"));
}

void TestBson::rejectsInvalidData_data()
{
    QTest::addColumn<QString>("hex");

    QTest::newRow("empty") << "";
    QTest::newRow("shorter than a document") << "04000000";
    QTest::newRow("empty document") << "0500000000";
    QTest::newRow("length too large") << "0d0000001061000100000000";
    QTest::newRow("length too small") << "0b0000001061000100000000";
    QTest::newRow("negative length") << "ffffffff1061000100000000";
    QTest::newRow("missing terminator") << "0c0000001061000100000001";
    QTest::newRow("trailing byte") << "0c000000106100010000000000";
    QTest::newRow("truncated element") << "090000001061000100";
    QTest::newRow("unknown element type") << "0800000020610000";
    QTest::newRow("key without terminator") << "090000000a61626300";
    QTest::newRow("string without terminator") << "0e00000002730002000000616200";
    QTest::newRow("string length zero") << "0c0000000273000000000000";
    QTest::newRow("string length beyond the document") << "0f0000000273006400000061620000";
    QTest::newRow("negative string length") << "0f000000027300fbffffff61620000";
    QTest::newRow("invalid boolean") << "090000000862000200";
    QTest::newRow("invalid utf-8 in string") << "0f00000002730003000000c3280000";
    QTest::newRow("invalid utf-8 in key") << "090000000afffe0000";
    QTest::newRow("binary length beyond the document") << "0f0000000562006400000000616200";
    QTest::newRow("old binary subtype with wrong inner length") << "120000000562000500000002090000006100";
    QTest::newRow("nested document with wrong length") << "0d000000036100320000000000";
    QTest::newRow("javascript scope length mismatch") << "190000000f6300630000000400000066282900050000000000";
    QTest::newRow("decimal128 truncated") << "10000000136400000000000000000000";
}

void TestBson::rejectsInvalidData()
{
    QFETCH(QString, hex);

    const QByteArray data = QByteArray::fromHex(hex.toLatin1());
    QString text;
    QVERIFY2(!bsonToText(data, text), qPrintable("invalid data was accepted: " + hex + " became " + text));
}

void TestBson::rejectsOtherFormats_data()
{
    QTest::addColumn<QByteArray>("data");

    QTest::newRow("text") << QByteArray("hello world");
    QTest::newRow("json object") << QByteArray("{\"a\":1}");
    QTest::newRow("json array") << QByteArray("[1,2,3]");
    QTest::newRow("single byte") << QByteArray("\x01", 1);
    QTest::newRow("zeros") << QByteArray(64, '\0');
    QTest::newRow("all bytes") << allBytes();
    // SQLite's JSONB encoding of the array [1,2] and of the object {"a":1}
    QTest::newRow("jsonb array") << QByteArray::fromHex("5b13311332");
    QTest::newRow("jsonb object") << QByteArray::fromHex("4c17611331");
    // PNG header
    QTest::newRow("png") << QByteArray::fromHex("89504e470d0a1a0a0000000d49484452");
}

void TestBson::rejectsOtherFormats()
{
    QFETCH(QByteArray, data);

    QString text;
    QVERIFY2(!bsonToText(data, text), qPrintable("data in another format was taken for BSON and became " + text));
}

void TestBson::leavesOutputAloneOnFailure()
{
    QString text = "unchanged";
    QVERIFY(!bsonToText(QByteArray("hello world"), text));
    QCOMPARE(text, QString("unchanged"));

    // Also if the problem is found after the output has been started
    QVERIFY(!bsonToText(document(element('\x10', "a", int32le(1)) + element('\x20', "b", QByteArray())), text));
    QCOMPARE(text, QString("unchanged"));
}

void TestBson::limitsNestingDepth_data()
{
    QTest::addColumn<int>("levels");
    QTest::addColumn<bool>("accepted");

    // The same limit as MongoDB has
    QTest::newRow("one level") << 1 << true;
    QTest::newRow("100 levels") << 100 << true;
    QTest::newRow("101 levels") << 101 << false;
    // This would use up the stack if the nesting wasn't limited
    QTest::newRow("200000 levels") << 200000 << false;
}

void TestBson::limitsNestingDepth()
{
    QFETCH(int, levels);
    QFETCH(bool, accepted);

    QString text;
    QCOMPARE(bsonToText(nestedDocument(levels), text), accepted);
    if(accepted)
    {
        const QString expected = QString("{\"d\":").repeated(levels - 1) + "{\"n\":null}" + QString("}").repeated(levels - 1);
        QCOMPARE(text, expected);
    }
}

void TestBson::limitsSize()
{
    // Documents are limited to 16 MiB in MongoDB, so there is no point in converting anything bigger into text
    QString text;
    QVERIFY(bsonToText(document(element('\x02', "k", stringValue(QByteArray(1024 * 1024, 'x')))), text));
    QVERIFY(!bsonToText(document(element('\x02', "k", stringValue(QByteArray(17 * 1024 * 1024, 'x')))), text));
}
