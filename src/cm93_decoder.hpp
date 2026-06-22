// SPDX-License-Identifier: GPL-2.0-or-later
//
// CM93 cell binary decoder. Ported from OpenCPN (cm93.cpp, Copyright (C) 2010
// David S. Register), GPL-2.0-or-later, de-wxWidgets-ified and reduced to the
// decode path (no rendering / viewport / offset-dialog machinery). Output
// geometry is in geographic degrees (lon/lat); the caller projects to Mercator.
// See README.md / COPYING.
#pragma once
#include <QString>
#include <string>
#include <utility>
#include <vector>

class Cm93Dictionary;

// A geographic point (degrees).
struct Cm93LL {
    double lat = 0.0;
    double lon = 0.0;
};

// One decoded sounding (geographic position + depth in metres).
struct Cm93Sounding {
    double lat = 0.0;
    double lon = 0.0;
    double depth = 0.0;
};

// One decoded CM93 feature object, geometry already converted to lon/lat.
struct Cm93Object {
    std::string objClass;     // S-57 acronym (e.g. "DEPARE", "BOYLAT")
    int geomPrim = 0;         // 1 point, 2 line, 4 area, 8 sounding(multipoint)
    // Area: exterior + interior rings. Line: rings[0] is the polyline. Point:
    // rings[0][0] is the position. (Soundings use `soundings` instead.)
    std::vector<std::vector<Cm93LL>> rings;
    std::vector<Cm93Sounding>        soundings;   // geomPrim == 8
    // S-57-style (acronym, value) attribute pairs.
    std::vector<std::pair<std::string, std::string>> attrs;
};

// Cheaply read a cell's geographic bounding box from its header (no full decode,
// no geometry). lon/lat in degrees, continuous (a seam-crossing cell may report
// lonMax > 180). Returns false if the file is missing or not a valid CM93 cell.
bool Cm93ReadCellExtent(const QString& path,
                        double& lonMin, double& latMin,
                        double& lonMax, double& latMax);

// Full footprint: the header bbox PLUS the cell's M_COVR coverage outline(s) as
// lon/lat rings (each transformed with its own datum offset). Requires a loaded
// dictionary and a full cell decode (heavier than Cm93ReadCellExtent — callers
// should cache the result). Coverage is empty when the cell has no M_COVR (treat
// the bbox as coverage). Returns false on a missing/invalid cell.
bool Cm93ReadCellFootprint(const QString& path, const Cm93Dictionary& dict,
                           double& lonMin, double& latMin,
                           double& lonMax, double& latMax,
                           std::vector<std::vector<Cm93LL>>& coverage, QString& err);

// Fully decode a CM93 cell file into objects. `dict` must be loaded. Thread-safe
// (uses no shared mutable state). Returns false (and sets err) on failure.
bool Cm93DecodeCell(const QString& path, const Cm93Dictionary& dict,
                    std::vector<Cm93Object>& out, QString& err);
