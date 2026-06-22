// SPDX-License-Identifier: GPL-2.0-or-later
//
// Standalone decoder smoke test (no GUI, no host). Verifies dictionary load +
// Cm93ReadCellFootprint + Cm93DecodeCell against a real CM93 dataset.
// Usage: test_cm93 <CM93root>
#include "cm93_dictionary.hpp"
#include "cm93_decoder.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QString>

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: test_cm93 <CM93root>\n"); return 2; }
    const QString root = QString::fromLocal8Bit(argv[1]);

    const QString dictDir = Cm93FindDictionaryDir(root);
    std::printf("dictDir: %s\n", dictDir.toLocal8Bit().constData());
    Cm93Dictionary dict;
    const bool dok = dict.load(dictDir);
    std::printf("dict loaded: %d\n", dok);
    if (!dok) { std::printf("FAIL: no dictionary\n"); return 1; }

    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    int perScale[128] = {0};
    int tested = 0, footOk = 0, decOk = 0;
    while (it.hasNext() && tested < 24) {
        const QString p = it.next();
        const QFileInfo fi(p);
        const QString suf = fi.suffix();
        if (suf.size() != 1) continue;
        const char sc = suf.at(0).toUpper().toLatin1();
        if (!std::strchr("ZABCDEFG", sc)) continue;
        if (perScale[(int)sc] >= 3) continue;
        perScale[(int)sc]++;
        ++tested;

        double lonMin, latMin, lonMax, latMax;
        std::vector<std::vector<Cm93LL>> cov; QString e;
        const bool fok = Cm93ReadCellFootprint(p, dict, lonMin, latMin, lonMax, latMax, cov, e);
        if (fok) ++footOk;
        int covPts = 0; for (auto& r : cov) covPts += (int)r.size();

        std::vector<Cm93Object> objs; QString e2;
        const bool d2 = Cm93DecodeCell(p, dict, objs, e2);
        if (d2) ++decOk;
        int narea = 0, nline = 0, npoint = 0, nsound = 0;
        for (auto& o : objs) {
            if (o.geomPrim == 8) nsound += (int)o.soundings.size();
            else if (o.geomPrim == 4) ++narea;
            else if (o.geomPrim == 2) ++nline;
            else ++npoint;
        }
        std::printf("[%c] %-14s foot=%d bbox=(%.3f,%.3f..%.3f,%.3f) covRings=%zu covPts=%d | dec=%d objs=%zu a=%d l=%d p=%d snd=%d %s\n",
                    sc, fi.fileName().toLocal8Bit().constData(), fok,
                    lonMin, latMin, lonMax, latMax, cov.size(), covPts,
                    d2, objs.size(), narea, nline, npoint, nsound,
                    fok ? "" : ("ERR:" + e).toLocal8Bit().constData());
    }
    std::printf("\nTESTED=%d footprintOk=%d decodeOk=%d\n", tested, footOk, decOk);
    return (tested > 0 && footOk == tested && decOk == tested) ? 0 : 1;
}
