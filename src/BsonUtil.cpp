#include "BsonUtil.h"

#include <QtGlobal>

#include <cmath>
#include <cstring>
#include <vector>

namespace
{

// MongoDB's limits for a single document and for the nesting of documents
const int MaxSize = 16 * 1024 * 1024;
const int MaxDepth = 100;

void appendJsonString(QString& out, const QString& str)
{
    out += '"';
    for(const QChar c : str)
    {
        switch(c.unicode())
        {
        case '"':   out += "\\\""; break;
        case '\\':  out += "\\\\"; break;
        case '\b':  out += "\\b"; break;
        case '\f':  out += "\\f"; break;
        case '\n':  out += "\\n"; break;
        case '\r':  out += "\\r"; break;
        case '\t':  out += "\\t"; break;
        default:
            if(c.unicode() < 0x20)
                out += "\\u" + QString::number(c.unicode(), 16).rightJustified(4, '0');
            else
                out += c;
        }
    }
    out += '"';
}

// Days since 1970-01-01 to the date in the Gregorian calendar, for dates from 1970 on
void civilFromDays(qint64 days, int& year, int& month, int& day)
{
    const qint64 z = days + 719468;
    const qint64 era = z / 146097;
    const qint64 doe = z - era * 146097;
    const qint64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const qint64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const qint64 mp = (5 * doy + 2) / 153;
    day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    year = static_cast<int>(yoe + era * 400 + (month <= 2 ? 1 : 0));
}

QString doubleToJson(double value)
{
    if(std::isnan(value))
        return "{\"$numberDouble\":\"NaN\"}";
    if(std::isinf(value))
        return value > 0 ? "{\"$numberDouble\":\"Infinity\"}" : "{\"$numberDouble\":\"-Infinity\"}";

    // Use the shortest text which gives back the same number
    QString str;
    for(int precision=15; precision<=17; ++precision)
    {
        str = QString::number(value, 'g', precision);
        if(str.toDouble() == value)
            break;
    }

    // Keep floating point numbers distinguishable from integers
    if(!str.contains('.') && !str.contains('e'))
        str += ".0";
    return str;
}

// The 128 bit decimal in the format used by BSON (BID encoding) as text, following the rules of the BSON decimal128 specification
QString decimal128ToString(const unsigned char* bytes)
{
    quint64 low = 0, high = 0;
    for(int i=7;i>=0;--i)
    {
        low = (low << 8) | bytes[i];
        high = (high << 8) | bytes[8 + i];
    }

    const bool negative = (high >> 63) != 0;
    const QString sign = negative ? "-" : "";

    // The combination field: 11110 is infinity, 11111 is NaN
    const unsigned combination = static_cast<unsigned>((high >> 58) & 0x1F);
    if(combination == 0x1F)
        return "NaN";
    if(combination == 0x1E)
        return sign + "Infinity";

    int exponent;
    quint32 limbs[4] = {0, 0, 0, 0};
    if(((high >> 61) & 3) != 3)
    {
        exponent = static_cast<int>((high >> 49) & 0x3FFF);
        const quint64 upper = high & 0x1FFFFFFFFFFFFULL;
        limbs[0] = static_cast<quint32>(low & 0xFFFFFFFF);
        limbs[1] = static_cast<quint32>(low >> 32);
        limbs[2] = static_cast<quint32>(upper & 0xFFFFFFFF);
        limbs[3] = static_cast<quint32>(upper >> 32);
    } else {
        // Significands that are too large to be valid are treated as 0
        exponent = static_cast<int>((high >> 47) & 0x3FFF);
    }
    exponent -= 6176;

    // Convert the significand to decimal digits by dividing it by 10^9 until nothing is left
    std::vector<quint32> chunks;
    bool nonzero = true;
    while(nonzero)
    {
        quint64 remainder = 0;
        nonzero = false;
        for(int i=3;i>=0;--i)
        {
            const quint64 current = (remainder << 32) | limbs[i];
            limbs[i] = static_cast<quint32>(current / 1000000000);
            remainder = current % 1000000000;
            if(limbs[i])
                nonzero = true;
        }
        chunks.push_back(static_cast<quint32>(remainder));
    }
    QString digits = QString::number(chunks.back());
    for(size_t i=chunks.size()-1;i>0;--i)
        digits += QString::number(chunks[i-1]).rightJustified(9, '0');

    const int length = digits.size();
    const int adjusted = exponent + length - 1;
    if(exponent == 0)
        return sign + digits;

    if(exponent > 0 || adjusted < -6)
    {
        // Scientific notation
        QString result = sign + digits.left(1);
        if(length > 1)
            result += '.' + digits.mid(1);
        return result + 'E' + (adjusted >= 0 ? '+' : '-') + QString::number(qAbs(adjusted));
    }

    // Plain notation with a decimal point
    if(length > -exponent)
        return sign + digits.left(length + exponent) + '.' + digits.right(-exponent);
    return sign + "0." + QString(-exponent - length, '0') + digits;
}

class BsonDecoder
{
public:
    BsonDecoder(const QByteArray& data) : d(reinterpret_cast<const unsigned char*>(data.constData())) {}

    // Decodes the document which starts at the given position and has to end at or before 'end'
    bool document(int& pos, int end, bool isArray, int depth, QString& out) const
    {
        if(depth > MaxDepth)
            return false;

        qint32 length;
        if(!readInt32(pos, end, length) || length < 5 || length > end - pos)
            return false;

        // The last byte of the document is a terminator which is not part of the elements
        const int docEnd = pos + length;
        const int elementsEnd = docEnd - 1;
        if(d[elementsEnd] != 0)
            return false;

        int p = pos + 4;
        out += isArray ? '[' : '{';
        bool first = true;
        while(p < elementsEnd)
        {
            const unsigned char type = d[p++];

            QString name;
            if(!readCString(p, elementsEnd, name))
                return false;

            if(!first)
                out += ',';
            first = false;
            if(!isArray)
            {
                appendJsonString(out, name);
                out += ':';
            }

            if(!value(type, p, elementsEnd, depth, out))
                return false;
        }
        if(p != elementsEnd)
            return false;

        out += isArray ? ']' : '}';
        pos = docEnd;
        return true;
    }

private:
    const unsigned char* d;

    bool readInt32(int pos, int end, qint32& result) const
    {
        if(end - pos < 4)
            return false;
        result = static_cast<qint32>(readUInt32(pos));
        return true;
    }

    quint32 readUInt32(int pos) const
    {
        return static_cast<quint32>(d[pos]) | (static_cast<quint32>(d[pos+1]) << 8) | (static_cast<quint32>(d[pos+2]) << 16) | (static_cast<quint32>(d[pos+3]) << 24);
    }

    quint64 readUInt64(int pos) const
    {
        return static_cast<quint64>(readUInt32(pos)) | (static_cast<quint64>(readUInt32(pos + 4)) << 32);
    }

    // Converts UTF-8 text and fails if it isn't valid UTF-8, which is the case if the text can't be converted back
    static bool fromUtf8(const unsigned char* bytes, int size, QString& result)
    {
        result = QString::fromUtf8(reinterpret_cast<const char*>(bytes), size);
        return result.toUtf8() == QByteArray::fromRawData(reinterpret_cast<const char*>(bytes), size);
    }

    // A string which ends with a null byte
    bool readCString(int& pos, int end, QString& result) const
    {
        int p = pos;
        while(p < end && d[p] != 0)
            ++p;
        if(p >= end || !fromUtf8(d + pos, p - pos, result))
            return false;

        pos = p + 1;
        return true;
    }

    // A string with its length in front of it, which is the number of bytes including the terminating null byte
    bool readString(int& pos, int end, QString& result) const
    {
        qint32 length;
        if(!readInt32(pos, end, length) || length < 1 || length > end - pos - 4 || d[pos + 4 + length - 1] != 0)
            return false;
        if(!fromUtf8(d + pos + 4, length - 1, result))
            return false;
        pos += 4 + length;
        return true;
    }

    bool objectId(int& pos, int end, QString& result) const
    {
        if(end - pos < 12)
            return false;
        result = QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(d + pos), 12).toHex());
        pos += 12;
        return true;
    }

    bool value(unsigned char type, int& p, int end, int depth, QString& out) const
    {
        switch(type)
        {
        case 0x01:  // Double
            if(end - p < 8)
                return false;
            {
                const quint64 bits = readUInt64(p);
                double number;
                std::memcpy(&number, &bits, sizeof(number));
                out += doubleToJson(number);
            }
            p += 8;
            return true;

        case 0x02:  // String
            {
                QString str;
                if(!readString(p, end, str))
                    return false;
                appendJsonString(out, str);
                return true;
            }

        case 0x03:  // Document
            return document(p, end, false, depth + 1, out);

        case 0x04:  // Array
            return document(p, end, true, depth + 1, out);

        case 0x05:  // Binary data
            {
                qint32 length;
                if(!readInt32(p, end, length) || length < 0 || end - p - 5 < length)
                    return false;
                const unsigned char subtype = d[p + 4];
                int dataStart = p + 5;
                int dataLength = length;
                if(subtype == 2)
                {
                    // The old binary subtype has a second length in front of the data
                    qint32 inner;
                    if(!readInt32(dataStart, dataStart + length, inner) || inner != length - 4)
                        return false;
                    dataStart += 4;
                    dataLength -= 4;
                }
                const QByteArray bytes(reinterpret_cast<const char*>(d + dataStart), dataLength);
                out += "{\"$binary\":{\"base64\":\"" + QString::fromLatin1(bytes.toBase64()) + "\",\"subType\":\"" +
                        QString::number(subtype, 16).rightJustified(2, '0') + "\"}}";
                p += 5 + length;
                return true;
            }

        case 0x06:  // Undefined (deprecated)
            out += "{\"$undefined\":true}";
            return true;

        case 0x07:  // ObjectId
            {
                QString oid;
                if(!objectId(p, end, oid))
                    return false;
                out += "{\"$oid\":\"" + oid + "\"}";
                return true;
            }

        case 0x08:  // Boolean
            if(end - p < 1 || d[p] > 1)
                return false;
            out += d[p] ? "true" : "false";
            p += 1;
            return true;

        case 0x09:  // Date and time as milliseconds since 1970-01-01
            {
                if(end - p < 8)
                    return false;
                const qint64 ms = static_cast<qint64>(readUInt64(p));
                p += 8;

                // The relaxed notation only covers the years 1970 to 9999
                if(ms < 0 || ms > 253402300799999LL)
                {
                    out += "{\"$date\":{\"$numberLong\":\"" + QString::number(ms) + "\"}}";
                    return true;
                }
                int year, month, day;
                civilFromDays(ms / 86400000, year, month, day);
                const int msOfDay = static_cast<int>(ms % 86400000);
                const auto two = [](int number) { return QString::number(number).rightJustified(2, '0'); };
                out += "{\"$date\":\"" + QString::number(year).rightJustified(4, '0') + '-' + two(month) + '-' + two(day) + 'T' +
                        two(msOfDay / 3600000) + ':' + two(msOfDay / 60000 % 60) + ':' + two(msOfDay / 1000 % 60);
                if(msOfDay % 1000)
                    out += '.' + QString::number(msOfDay % 1000).rightJustified(3, '0');
                out += "Z\"}";
                return true;
            }

        case 0x0A:  // Null
            out += "null";
            return true;

        case 0x0B:  // Regular expression
            {
                QString pattern, options;
                if(!readCString(p, end, pattern) || !readCString(p, end, options))
                    return false;
                out += "{\"$regularExpression\":{\"pattern\":";
                appendJsonString(out, pattern);
                out += ",\"options\":";
                appendJsonString(out, options);
                out += "}}";
                return true;
            }

        case 0x0C:  // DBPointer (deprecated)
            {
                QString ref, oid;
                if(!readString(p, end, ref) || !objectId(p, end, oid))
                    return false;
                out += "{\"$dbPointer\":{\"$ref\":";
                appendJsonString(out, ref);
                out += ",\"$id\":{\"$oid\":\"" + oid + "\"}}}";
                return true;
            }

        case 0x0D:  // JavaScript code
        case 0x0E:  // Symbol (deprecated)
            {
                QString str;
                if(!readString(p, end, str))
                    return false;
                out += type == 0x0D ? "{\"$code\":" : "{\"$symbol\":";
                appendJsonString(out, str);
                out += '}';
                return true;
            }

        case 0x0F:  // JavaScript code with scope (deprecated)
            {
                qint32 length;
                if(!readInt32(p, end, length) || length < 14 || length > end - p)
                    return false;
                const int codeEnd = p + length;
                int q = p + 4;
                QString code;
                if(!readString(q, codeEnd, code))
                    return false;
                out += "{\"$code\":";
                appendJsonString(out, code);
                out += ",\"$scope\":";
                if(!document(q, codeEnd, false, depth + 1, out) || q != codeEnd)
                    return false;
                out += '}';
                p = codeEnd;
                return true;
            }

        case 0x10:  // 32 bit integer
            {
                qint32 number;
                if(!readInt32(p, end, number))
                    return false;
                out += QString::number(number);
                p += 4;
                return true;
            }

        case 0x11:  // Timestamp: the increment is stored in front of the seconds
            if(end - p < 8)
                return false;
            out += "{\"$timestamp\":{\"t\":" + QString::number(readUInt32(p + 4)) + ",\"i\":" + QString::number(readUInt32(p)) + "}}";
            p += 8;
            return true;

        case 0x12:  // 64 bit integer
            if(end - p < 8)
                return false;
            out += QString::number(static_cast<qint64>(readUInt64(p)));
            p += 8;
            return true;

        case 0x13:  // 128 bit decimal
            if(end - p < 16)
                return false;
            out += "{\"$numberDecimal\":\"" + decimal128ToString(d + p) + "\"}";
            p += 16;
            return true;

        case 0x7F:  // Max key
            out += "{\"$maxKey\":1}";
            return true;

        case 0xFF:  // Min key
            out += "{\"$minKey\":1}";
            return true;

        default:
            return false;
        }
    }
};

}

bool bsonToText(const QByteArray& data, QString& text)
{
    // The smallest document with an element has a length, a type, a one character name and a terminator
    if(data.size() <= 5 || data.size() > MaxSize)
        return false;

    QString result;
    int pos = 0;
    const BsonDecoder decoder(data);
    if(!decoder.document(pos, data.size(), false, 1, result) || pos != data.size())
        return false;

    text = result;
    return true;
}
