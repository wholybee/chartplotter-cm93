// SPDX-License-Identifier: GPL-2.0-or-later
//
// CM93 chart-source plugin for HMV Chartplotter.
// Copyright (C) 2026 Warren Holybee
// CM93 decoding derived from OpenCPN (cm93.cpp, Copyright (C) 2010
// David S. Register), GPL-2.0-or-later. See README.md / COPYING.
#include "cm93_chart_source.hpp"
#include "cm93_decoder.hpp"

#include "projection.hpp"   // host SDK: proj::lonToX / latToY

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentMap>

#include <algorithm>
#include <atomic>
#include <cstdlib>

namespace {

// The eight CM93 scale characters, coarsest (Z, ~world) to finest (G, ~harbour).
bool isScaleChar(QChar c) {
    static const QString kScales = QStringLiteral("ZABCDEFG");
    return kScales.contains(c.toUpper());
}

// A CM93 cell file: single scale-char extension and a (mostly) numeric basename
// (the leading char may be a subcell letter, the rest are geographic digits).
bool looksLikeCellFile(const QFileInfo& fi) {
    const QString suffix = fi.suffix();
    if (suffix.size() != 1 || !isScaleChar(suffix.at(0))) return false;
    const QString base = fi.completeBaseName();
    if (base.size() < 6) return false;
    for (int i = 1; i < base.size(); ++i)   // allow non-digit first char (subcell)
        if (!base.at(i).isDigit()) return false;
    return true;
}

// Map a CM93 scale char onto the host's band model (ChartView::kMaxBand == 8).
// Each of CM93's 8 scales gets its OWN band: CM93 is a global database where
// every scale covers the whole world, so if two overlapping scales shared a
// band the quilt would treat them as non-overlapping tiles and draw both,
// double-drawing features and flickering as you zoom. Distinct bands let the
// quilt occlude coarser scales with finer ones cleanly.
int bandForScaleChar(QChar c) {
    switch (c.toUpper().toLatin1()) {
        case 'Z': return 1;   // ~1:20,000,000  world
        case 'A': return 2;   // ~1:3,000,000
        case 'B': return 3;   // ~1:1,000,000
        case 'C': return 4;   // ~1:200,000
        case 'D': return 5;   // ~1:100,000
        case 'E': return 6;   // ~1:50,000
        case 'F': return 7;   // ~1:20,000
        case 'G': return 8;   // ~1:7,500     harbour
        default:  return 0;
    }
}

// ---- Feature classification (mirrors the host's ENC chart_loader) -----------
FeatureKind classify(const std::string& n, int geomPrim) {
    if (n == "DEPARE" || n == "DRGARE") return FeatureKind::DepthArea;
    if (n == "LNDARE")                  return FeatureKind::LandArea;
    if (n == "COALNE" || n == "SLCONS") return FeatureKind::Coastline;
    if (n == "DEPCNT")                  return FeatureKind::DepthContour;
    if (n == "SOUNDG")                  return FeatureKind::Sounding;
    if (geomPrim == 4) return FeatureKind::OtherArea;
    if (geomPrim == 2) return FeatureKind::OtherLine;
    return FeatureKind::Point;
}

int zorderFor(FeatureKind k) {
    switch (k) {
        case FeatureKind::DepthArea:    return 0;
        case FeatureKind::OtherArea:    return 5;
        case FeatureKind::LandArea:     return 10;
        case FeatureKind::OtherLine:    return 20;
        case FeatureKind::DepthContour: return 21;
        case FeatureKind::Coastline:    return 22;
        case FeatureKind::Sounding:     return 30;
        case FeatureKind::Point:        return 40;
    }
    return 50;
}

Pt project(const Cm93LL& ll) {
    return Pt{ proj::lonToX(ll.lon), proj::latToY(ll.lat) };
}

bool hasDictionary(const QString& dir) {
    const QDir d(dir);
    return d.exists(QStringLiteral("CM93OBJ.DIC")) ||
           d.exists(QStringLiteral("cm93obj.dic")) ||
           d.exists(QStringLiteral("CM93ATTR/CM93OBJ.DIC"));
}

// ---- on-disk catalog cache --------------------------------------------------
// CM93 footprints (bbox + M_COVR coverage) are expensive to compute (each needs
// a full cell decode over a ~1.9 GB / 30k-cell dataset), so they are cached to
// disk keyed per cell by path+size+mtime. Subsequent scans only re-decode cells
// whose file changed.
struct CachedCell {
    qint64 size = 0;
    qint64 mtime = 0;
    int    band = 0;
    BBox   bbox;
    std::vector<std::vector<Pt>> coverage;
};

constexpr quint32 kCacheMagic   = 0x434D3933;   // 'CM93'
constexpr qint32  kCacheVersion = 1;

QString cacheFilePath(const QString& root) {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/cm93_catalog_cache");
    QDir().mkpath(dir);
    const QByteArray h = QCryptographicHash::hash(root.toUtf8(), QCryptographicHash::Sha1).toHex();
    return dir + QLatin1Char('/') + QString::fromLatin1(h) + QStringLiteral(".bin");
}

QHash<QString, CachedCell> loadCatalogCache(const QString& root) {
    QHash<QString, CachedCell> map;
    QFile f(cacheFilePath(root));
    if (!f.open(QIODevice::ReadOnly)) return map;
    QDataStream ds(&f);
    ds.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0; qint32 ver = 0, count = 0;
    ds >> magic >> ver >> count;
    if (magic != kCacheMagic || ver != kCacheVersion) return {};
    map.reserve(count);
    for (qint32 i = 0; i < count; ++i) {
        QString path; CachedCell c; qint32 nRings = 0;
        ds >> path >> c.size >> c.mtime >> c.band
           >> c.bbox.minx >> c.bbox.miny >> c.bbox.maxx >> c.bbox.maxy >> nRings;
        if (ds.status() != QDataStream::Ok) return {};
        for (qint32 r = 0; r < nRings; ++r) {
            qint32 nPts = 0; ds >> nPts;
            std::vector<Pt> ring; ring.reserve(nPts);
            for (qint32 p = 0; p < nPts; ++p) { double x, y; ds >> x >> y; ring.push_back({x, y}); }
            if (ring.size() >= 3) c.coverage.push_back(std::move(ring));
        }
        if (ds.status() != QDataStream::Ok) return {};
        map.insert(path, std::move(c));
    }
    return map;
}

void saveCatalogCache(const QString& root, const QHash<QString, CachedCell>& map) {
    QFile f(cacheFilePath(root));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    QDataStream ds(&f);
    ds.setVersion(QDataStream::Qt_6_0);
    ds << kCacheMagic << kCacheVersion << (qint32)map.size();
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        const CachedCell& c = it.value();
        ds << it.key() << c.size << c.mtime << c.band
           << c.bbox.minx << c.bbox.miny << c.bbox.maxx << c.bbox.maxy
           << (qint32)c.coverage.size();
        for (const std::vector<Pt>& ring : c.coverage) {
            ds << (qint32)ring.size();
            for (const Pt& p : ring) ds << p.x << p.y;
        }
    }
}

} // namespace

bool Cm93ChartSource::canHandle(const QString& root) const {
    if (root.isEmpty()) return false;
    if (!QFileInfo(root).isDir()) return false;

    // Strongest signal: the CM93 dictionary at/near the root.
    if (!Cm93FindDictionaryDir(root).isEmpty()) return true;

    // Fallback: a CM93 cell file somewhere in the tree (bounded scan).
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    int filesChecked = 0;
    while (it.hasNext() && filesChecked < 20000) {
        const QFileInfo fi(it.next());
        if (looksLikeCellFile(fi)) return true;
        ++filesChecked;
    }
    return false;
}

bool Cm93ChartSource::ensureDict(const QString& nearPath, QString& errMsg) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (dictLoaded_) return true;

    const QFileInfo fi(nearPath);
    const QString startDir = fi.isDir() ? nearPath : fi.absolutePath();
    const QString dictDir = Cm93FindDictionaryDir(startDir);
    if (dictDir.isEmpty()) {
        errMsg = QStringLiteral("CM93 dictionary (CM93OBJ.DIC) not found near ") + nearPath;
        return false;
    }
    if (!dict_.load(dictDir)) {
        errMsg = QStringLiteral("failed to load CM93 dictionary from ") + dictDir;
        return false;
    }
    dictLoaded_ = true;
    return true;
}

bool Cm93ChartSource::catalog(const QString& root,
                              std::vector<ChartSourceCell>& out, QString& errMsg,
                              const std::function<void(int, int)>& progress) {
    out.clear();

    // The dictionary is needed to identify M_COVR objects for the coverage rings;
    // if it's absent we degrade to header-bbox-only footprints (still usable).
    QString dictErr;
    ensureDict(root, dictErr);
    const Cm93Dictionary* dict = dictLoaded_ ? &dict_ : nullptr;

    // 1) Enumerate cell files (+ stat for cache staleness).
    struct FileItem { QString path; QChar scale; qint64 size; qint64 mtime; };
    std::vector<FileItem> files;
    {
        QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString p = it.next();
            const QFileInfo fi(p);
            if (!looksLikeCellFile(fi)) continue;
            files.push_back({ p, fi.suffix().at(0), fi.size(),
                              fi.lastModified().toSecsSinceEpoch() });
        }
    }
    if (files.empty()) { errMsg = QStringLiteral("No CM93 cells found under:\n") + root; return false; }
    const int total = static_cast<int>(files.size());

    // 2) Reuse cached footprints where the file is unchanged; decode the rest.
    const QHash<QString, CachedCell> cache = loadCatalogCache(root);
    std::vector<ChartSourceCell> cells(files.size());
    std::vector<int> toDecode;
    toDecode.reserve(files.size());
    for (int i = 0; i < total; ++i) {
        const FileItem& fi = files[i];
        auto hit = cache.constFind(fi.path);
        if (hit != cache.constEnd() && hit->size == fi.size && hit->mtime == fi.mtime) {
            cells[i].id = fi.path;
            cells[i].band = hit->band;
            cells[i].bbox = hit->bbox;
            cells[i].coverage = hit->coverage;
        } else {
            toDecode.push_back(i);
        }
    }

    // 3) Decode changed/new cells in parallel chunks (each cold cell reads +
    //    parses the whole file), reporting progress between chunks.
    std::atomic<int> done{ total - static_cast<int>(toDecode.size()) };
    if (progress) progress(done.load(), total);
    constexpr int kChunk = 256;
    for (std::size_t base = 0; base < toDecode.size(); base += kChunk) {
        const std::size_t stop = std::min(base + std::size_t(kChunk), toDecode.size());
        QtConcurrent::blockingMap(
            toDecode.begin() + base, toDecode.begin() + stop,
            [&](int idx) {
                const FileItem& fi = files[idx];
                ChartSourceCell& c = cells[idx];
                double lonMin, latMin, lonMax, latMax;
                std::vector<std::vector<Cm93LL>> cov;
                QString e;
                const bool ok = dict
                    ? Cm93ReadCellFootprint(fi.path, *dict, lonMin, latMin, lonMax, latMax, cov, e)
                    : Cm93ReadCellExtent(fi.path, lonMin, latMin, lonMax, latMax);
                if (ok) {
                    c.id = fi.path;
                    c.band = bandForScaleChar(fi.scale);
                    c.bbox.expand(proj::lonToX(lonMin), proj::latToY(latMin));
                    c.bbox.expand(proj::lonToX(lonMax), proj::latToY(latMax));
                    for (const std::vector<Cm93LL>& ring : cov) {
                        std::vector<Pt> pr;
                        pr.reserve(ring.size());
                        for (const Cm93LL& ll : ring)
                            pr.push_back({ proj::lonToX(ll.lon), proj::latToY(ll.lat) });
                        if (pr.size() >= 3) c.coverage.push_back(std::move(pr));
                    }
                }
                // ok == false leaves c.id empty -> dropped below.
                done.fetch_add(1, std::memory_order_relaxed);
            });
        if (progress) progress(done.load(), total);
    }

    // 4) Assemble output and rewrite the cache (valid cells only).
    QHash<QString, CachedCell> updated;
    updated.reserve(total);
    for (int i = 0; i < total; ++i) {
        ChartSourceCell& c = cells[i];
        if (c.id.isEmpty()) continue;   // failed decode
        CachedCell cc;
        cc.size = files[i].size; cc.mtime = files[i].mtime;
        cc.band = c.band; cc.bbox = c.bbox; cc.coverage = c.coverage;
        updated.insert(files[i].path, std::move(cc));
        out.push_back(std::move(c));
    }
    saveCatalogCache(root, updated);

    if (progress) progress(total, total);
    if (out.empty()) { errMsg = QStringLiteral("No usable CM93 cells under:\n") + root; return false; }
    return true;
}

bool Cm93ChartSource::loadCell(const QString& cellId,
                               std::vector<Feature>& out, BBox& bbox,
                               QString& errMsg) {
    if (!ensureDict(cellId, errMsg)) return false;

    std::vector<Cm93Object> objects;
    if (!Cm93DecodeCell(cellId, dict_, objects, errMsg)) return false;

    out.clear();
    out.reserve(objects.size());

    auto emitFeature = [&](Feature&& f) {
        if (f.rings.empty()) return;
        bbox.expand(f.bbox);
        out.push_back(std::move(f));
    };

    for (const Cm93Object& co : objects) {
        const FeatureKind kind = classify(co.objClass, co.geomPrim);

        // Soundings: one Feature per point (matches the host's split-multipoint
        // ENC handling), each carrying its own depth.
        if (kind == FeatureKind::Sounding) {
            for (const Cm93Sounding& s : co.soundings) {
                Feature f;
                f.kind = FeatureKind::Sounding;
                f.zorder = zorderFor(FeatureKind::Sounding);
                const Pt p = project({ s.lat, s.lon });
                f.bbox.expand(p.x, p.y);
                f.rings.push_back({ p });
                f.depth = s.depth;
                f.hasDepth = true;
                emitFeature(std::move(f));
            }
            continue;
        }

        Feature f;
        f.kind = kind;
        f.zorder = zorderFor(kind);
        for (const std::vector<Cm93LL>& ring : co.rings) {
            std::vector<Pt> projected;
            projected.reserve(ring.size());
            for (const Cm93LL& ll : ring) {
                const Pt p = project(ll);
                projected.push_back(p);
                f.bbox.expand(p.x, p.y);
            }
            if (!projected.empty()) f.rings.push_back(std::move(projected));
        }
        if (f.rings.empty()) continue;

        // Symbol-bearing kinds carry their object class + attributes so the host's
        // S-52 symbology engine can resolve them, exactly as for ENC features.
        const bool symbolBearing = (kind == FeatureKind::Point ||
                                    kind == FeatureKind::OtherArea ||
                                    kind == FeatureKind::OtherLine);
        if (symbolBearing) {
            f.objClass = co.objClass;
            f.attrs = co.attrs;
        }

        // Pull out the few attributes the host treats specially (same fields the
        // ENC loader reads): OBJNAM label, SCAMIN declutter floor, DEPARE depth.
        for (const auto& kv : co.attrs) {
            if (symbolBearing && kv.first == "OBJNAM") {
                f.name = kv.second;
            } else if (kv.first == "SCAMIN") {
                f.scaleMin = std::atoi(kv.second.c_str());
            } else if (kind == FeatureKind::DepthArea && kv.first == "DRVAL1") {
                f.depth = std::atof(kv.second.c_str());
                f.hasDepth = true;
            }
        }

        emitFeature(std::move(f));
    }

    if (out.empty()) {
        errMsg = QStringLiteral("CM93 cell produced no features: ") + cellId;
        return false;
    }
    return true;
}
