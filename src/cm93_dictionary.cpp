// SPDX-License-Identifier: GPL-2.0-or-later
//
// CM93 dictionary reader. Ported from OpenCPN cm93_dictionary (cm93.cpp,
// Copyright (C) 2010 David S. Register), GPL-2.0-or-later. See README.md.
#include "cm93_dictionary.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

namespace {

// CM93 attribute value-type token -> single-char type code (as OpenCPN's
// m_ValTypeArray). Anything else maps to '?'.
char attrTypeFromToken(const QString& tok) {
    const QString t = tok.trimmed();
    if (t == QLatin1String("aFLOAT"))  return 'R';
    if (t == QLatin1String("aBYTE"))   return 'B';
    if (t == QLatin1String("aSTRING")) return 'S';
    if (t == QLatin1String("aCMPLX"))  return 'C';
    if (t == QLatin1String("aLIST"))   return 'L';
    if (t == QLatin1String("aWORD10")) return 'W';
    if (t == QLatin1String("aLONG"))   return 'G';
    return '?';
}

// Read a .DIC file into trimmed lines. The dictionary files are plain ASCII.
bool readLines(const QString& path, QStringList& lines) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    const QByteArray all = f.readAll();
    lines = QString::fromLatin1(all).split(QLatin1Char('\n'));
    for (QString& s : lines)
        if (s.endsWith(QLatin1Char('\r'))) s.chop(1);
    return true;
}

// Case-insensitive existence check for a dictionary file, returning the actual
// path (CM93 datasets vary between upper- and lowercase names).
QString findFileCI(const QDir& dir, const QString& nameUpper) {
    if (dir.exists(nameUpper)) return dir.filePath(nameUpper);
    const QString lower = nameUpper.toLower();
    if (dir.exists(lower)) return dir.filePath(lower);
    return QString();
}

} // namespace

bool Cm93Dictionary::loadObjects(const QString& path) {
    QStringList lines;
    if (!readLines(path, lines)) return false;

    // First pass: find the maximum class code so the arrays can be sized once.
    int iclassMax = 0;
    for (const QString& line : lines) {
        const QStringList f = line.split(QLatin1Char('|'));
        if (f.size() < 2) continue;
        bool ok = false;
        const int iclass = f.at(1).toInt(&ok);
        if (ok && iclass > iclassMax) iclassMax = iclass;
    }
    maxClass_ = iclassMax;
    classArray_.assign(iclassMax + 1, std::string("NULLNM"));
    geomTypeArray_.assign(iclassMax + 1, -1);

    // Second pass: fill class acronyms + primary geometry type (A/L/P).
    for (const QString& line : lines) {
        const QStringList f = line.split(QLatin1Char('|'));
        if (f.size() < 3) continue;
        bool ok = false;
        const int iclass = f.at(1).toInt(&ok);
        if (!ok || iclass < 0 || iclass > maxClass_) continue;
        classArray_[iclass] = f.at(0).trimmed().toStdString();
        const QString geo = f.at(2).trimmed();
        if (!geo.isEmpty()) {
            switch (geo.at(0).toLatin1()) {
                case 'A': geomTypeArray_[iclass] = 3; break;
                case 'L': geomTypeArray_[iclass] = 2; break;
                case 'P': geomTypeArray_[iclass] = 1; break;
                default:  geomTypeArray_[iclass] = -1; break;
            }
        }
    }
    return maxClass_ >= 0;
}

bool Cm93Dictionary::loadAttributes(const QString& dir) {
    const QDir d(dir);

    // Two on-disk formats; the value-type token sits in a different column. Try
    // ATTRLUT.DIC (long form, type at field 5) first, then CM93ATTR.DIC (short
    // form, type at field 2).
    QString path = findFileCI(d, QStringLiteral("ATTRLUT.DIC"));
    int typeField = 5;
    if (path.isEmpty()) {
        path = findFileCI(d, QStringLiteral("CM93ATTR.DIC"));
        typeField = 2;
    }
    if (path.isEmpty()) return false;

    QStringList lines;
    if (!readLines(path, lines)) return false;

    int iattrMax = 0;
    for (const QString& line : lines) {
        if (line.startsWith(QLatin1Char(';'))) continue;
        const QStringList f = line.split(QLatin1Char('|'));
        if (f.size() < 2) continue;
        bool ok = false;
        const int iattr = f.at(1).toInt(&ok);
        if (ok && iattr > iattrMax) iattrMax = iattr;
    }
    maxAttr_ = iattrMax;
    attrArray_.assign(iattrMax + 1, std::string("NULLNM"));
    valTypeArray_.assign(iattrMax + 1, '?');

    for (const QString& line : lines) {
        if (line.startsWith(QLatin1Char(';'))) continue;
        const QStringList f = line.split(QLatin1Char('|'));
        if (f.size() < 2) continue;
        bool ok = false;
        const int iattr = f.at(1).toInt(&ok);
        if (!ok || iattr < 0 || iattr > maxAttr_) continue;
        attrArray_[iattr] = f.at(0).trimmed().toStdString();
        if (f.size() > typeField)
            valTypeArray_[iattr] = attrTypeFromToken(f.at(typeField));
    }
    return maxAttr_ >= 0;
}

bool Cm93Dictionary::load(const QString& dictDir) {
    ok_ = false;
    QString dir = dictDir;
    if (!dir.endsWith(QLatin1Char('/')) && !dir.endsWith(QLatin1Char('\\')))
        dir += QLatin1Char('/');
    dictDir_ = dir;

    const QString objPath = findFileCI(QDir(dir), QStringLiteral("CM93OBJ.DIC"));
    if (objPath.isEmpty()) return false;
    if (!loadObjects(objPath)) return false;
    if (!loadAttributes(dir)) return false;

    ok_ = true;
    return true;
}

std::string Cm93Dictionary::className(int iclass) const {
    if (iclass < 0 || iclass > maxClass_) return "Unknown";
    return classArray_[static_cast<std::size_t>(iclass)];
}

std::string Cm93Dictionary::attrName(int iattr) const {
    if (iattr < 0 || iattr > maxAttr_) return "UnknownAttr";
    return attrArray_[static_cast<std::size_t>(iattr)];
}

char Cm93Dictionary::attrType(int iattr) const {
    if (iattr < 0 || iattr > maxAttr_) return '?';
    return valTypeArray_[static_cast<std::size_t>(iattr)];
}

QString Cm93FindDictionaryDir(const QString& root) {
    // Check a directory and its CM93ATTR subdir for CM93OBJ.DIC (either case).
    auto hasObj = [](const QString& d) -> bool {
        const QDir dir(d);
        return !findFileCI(dir, QStringLiteral("CM93OBJ.DIC")).isEmpty();
    };

    QDir dir(root);
    // Walk up from root, at each level trying the level itself and its CM93ATTR
    // subdir, a handful of levels deep (datasets keep the dictionary at or above
    // the cell tree root).
    for (int depth = 0; depth < 6; ++depth) {
        const QString here = dir.absolutePath();
        if (hasObj(here)) return here;
        const QString attr = dir.filePath(QStringLiteral("CM93ATTR"));
        if (hasObj(attr)) return attr;
        if (!dir.cdUp()) break;
    }

    // Also try one level *down*: the user may have selected a parent that holds
    // the CM93 root as a child directory.
    QDir base(root);
    const QFileInfoList subs = base.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : subs) {
        if (hasObj(fi.absoluteFilePath())) return fi.absoluteFilePath();
        const QString attr = QDir(fi.absoluteFilePath()).filePath(QStringLiteral("CM93ATTR"));
        if (hasObj(attr)) return attr;
    }
    return QString();
}
