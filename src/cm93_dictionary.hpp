// SPDX-License-Identifier: GPL-2.0-or-later
//
// CM93 dictionary reader. Ported from OpenCPN's cm93_dictionary
// (gui/src/cm93.cpp, Copyright (C) 2010 David S. Register), GPL-2.0-or-later,
// de-wxWidgets-ified for HMV Chartplotter. See README.md / COPYING.
#pragma once
#include <QString>
#include <string>
#include <vector>

// Maps CM93's binary object-class and attribute integer codes to the S-57 text
// acronyms (DEPARE, SOUNDG, OBJNAM, ...) the rest of the app understands. The
// data comes from the plain-text dictionary files shipped with a CM93 dataset:
//   CM93OBJ.DIC   object classes:   NAME|code|geomtype|...
//   ATTRLUT.DIC   attributes (long): NAME|code|..|..|..|valuetype
//   CM93ATTR.DIC  attributes (short): NAME|code|valuetype       (alternate)
// Only one attribute file is present in a given dataset. These files are NOT
// byte-obfuscated (unlike the cell files).
class Cm93Dictionary {
public:
    Cm93Dictionary() = default;

    // Load from a directory containing the .DIC files. Returns true on success.
    bool load(const QString& dictDir);
    bool isOk() const { return ok_; }
    QString dictDir() const { return dictDir_; }

    // Object-class acronym for a class code (e.g. 42 -> "DEPARE"); "Unknown" if
    // out of range.
    std::string className(int iclass) const;
    // Attribute acronym for an attribute code (e.g. -> "OBJNAM"); "UnknownAttr"
    // if out of range.
    std::string attrName(int iattr) const;
    // Attribute value-type code: 'R' float, 'B' byte, 'S' string, 'C' complex,
    // 'L' list, 'W' word/10, 'G' long; '?' if unknown.
    char attrType(int iattr) const;

private:
    bool loadObjects(const QString& path);
    bool loadAttributes(const QString& dir);

    int maxClass_ = -1;
    int maxAttr_  = -1;
    std::vector<std::string> classArray_;   // [class code] -> acronym
    std::vector<int>         geomTypeArray_; // [class code] -> 1/2/3 (P/L/A), -1
    std::vector<std::string> attrArray_;     // [attr code]  -> acronym
    std::vector<char>        valTypeArray_;  // [attr code]  -> type char
    QString dictDir_;
    bool ok_ = false;
};

// Locate the directory holding CM93OBJ.DIC for a dataset rooted at (or near)
// `root`: checks root, root/CM93ATTR, and a few ancestor directories (and their
// CM93ATTR subdirs), mirroring OpenCPN's search. Empty string if not found.
QString Cm93FindDictionaryDir(const QString& root);
