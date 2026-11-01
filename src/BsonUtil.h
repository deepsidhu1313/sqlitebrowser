#ifndef BSONUTIL_H
#define BSONUTIL_H

#include <QByteArray>
#include <QString>

// Checks whether the data is a complete and well-formed BSON document, the binary format MongoDB uses (see https://bsonspec.org), and
// if so converts it to JSON text. The members keep the order they have in the document. The types JSON doesn't have are written in
// MongoDB's Extended JSON notation (relaxed mode), e.g. {"$oid": "..."} for an ObjectId or {"$date": "..."} for a date.
// The text is only changed if the data is BSON. Empty documents are not detected because any five byte blob that starts with a 5 and
// ends in 0 would be one. The same goes for the first four bytes of a lot of other data: they have to be the length of the data, the
// last byte has to be 0 and everything in between has to make sense. Nesting is limited to 100 levels and documents to 16 MiB, just as
// in MongoDB.
bool bsonToText(const QByteArray& data, QString& text);

#endif
