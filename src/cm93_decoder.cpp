// SPDX-License-Identifier: GPL-2.0-or-later
//
// CM93 cell binary decoder. Ported from OpenCPN (cm93.cpp, Copyright (C) 2010
// David S. Register), GPL-2.0-or-later. The byte-deobfuscation table, header /
// table layout, geometry assembly and the cell->lat/lon transform are taken
// from that source; the wxWidgets, rendering and viewport machinery are
// replaced with plain C++/Qt that emits geographic geometry. See README.md.
//
// NOTE: assumes a little-endian host (the only supported target is Windows
// x64), matching OpenCPN's element-wise reads.
#include "cm93_decoder.hpp"
#include "cm93_dictionary.hpp"

#include <QByteArray>
#include <QFile>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace {

constexpr double kPI = 3.14159265358979323846;
constexpr double kDegree = kPI / 180.0;
// CM93 georeferences on the International 1924 ellipsoid (semi-major axis), not
// WGS84. This constant is from OpenCPN (empirically matched to the cell box).
constexpr double kCm93SemiMajor = 6378388.0;

// Sanity cap on header-declared record counts, to reject corrupt cells before
// allocating. Generous: real cells are far below this.
constexpr int kMaxRecords = 4'000'000;

// ---- byte deobfuscation table (verbatim from OpenCPN) ----------------------
unsigned char Table_0[] = {
    0x0CD, 0x0EA, 0x0DC, 0x048, 0x03E, 0x06D, 0x0CA, 0x07B, 0x052, 0x0E1, 0x0A4,
    0x08E, 0x0AB, 0x005, 0x0A7, 0x097, 0x0B9, 0x060, 0x039, 0x085, 0x07C, 0x056,
    0x07A, 0x0BA, 0x068, 0x06E, 0x0F5, 0x05D, 0x002, 0x04E, 0x00F, 0x0A1, 0x027,
    0x024, 0x041, 0x034, 0x000, 0x05A, 0x0FE, 0x0CB, 0x0D0, 0x0FA, 0x0F8, 0x06C,
    0x074, 0x096, 0x09E, 0x00E, 0x0C2, 0x049, 0x0E3, 0x0E5, 0x0C0, 0x03B, 0x059,
    0x018, 0x0A9, 0x086, 0x08F, 0x030, 0x0C3, 0x0A8, 0x022, 0x00A, 0x014, 0x01A,
    0x0B2, 0x0C9, 0x0C7, 0x0ED, 0x0AA, 0x029, 0x094, 0x075, 0x00D, 0x0AC, 0x00C,
    0x0F4, 0x0BB, 0x0C5, 0x03F, 0x0FD, 0x0D9, 0x09C, 0x04F, 0x0D5, 0x084, 0x01E,
    0x0B1, 0x081, 0x069, 0x0B4, 0x009, 0x0B8, 0x03C, 0x0AF, 0x0A3, 0x008, 0x0BF,
    0x0E0, 0x09A, 0x0D7, 0x0F7, 0x08C, 0x067, 0x066, 0x0AE, 0x0D4, 0x04C, 0x0A5,
    0x0EC, 0x0F9, 0x0B6, 0x064, 0x078, 0x006, 0x05B, 0x09B, 0x0F2, 0x099, 0x0CE,
    0x0DB, 0x053, 0x055, 0x065, 0x08D, 0x007, 0x033, 0x004, 0x037, 0x092, 0x026,
    0x023, 0x0B5, 0x058, 0x0DA, 0x02F, 0x0B3, 0x040, 0x05E, 0x07F, 0x04B, 0x062,
    0x080, 0x0E4, 0x06F, 0x073, 0x01D, 0x0DF, 0x017, 0x0CC, 0x028, 0x025, 0x02D,
    0x0EE, 0x03A, 0x098, 0x0E2, 0x001, 0x0EB, 0x0DD, 0x0BC, 0x090, 0x0B0, 0x0FC,
    0x095, 0x076, 0x093, 0x046, 0x057, 0x02C, 0x02B, 0x050, 0x011, 0x00B, 0x0C1,
    0x0F0, 0x0E7, 0x0D6, 0x021, 0x031, 0x0DE, 0x0FF, 0x0D8, 0x012, 0x0A6, 0x04D,
    0x08A, 0x013, 0x043, 0x045, 0x038, 0x0D2, 0x087, 0x0A0, 0x0EF, 0x082, 0x0F1,
    0x047, 0x089, 0x06A, 0x0C8, 0x054, 0x01B, 0x016, 0x07E, 0x079, 0x0BD, 0x06B,
    0x091, 0x0A2, 0x071, 0x036, 0x0B7, 0x003, 0x03D, 0x072, 0x0C6, 0x044, 0x08B,
    0x0CF, 0x015, 0x09F, 0x032, 0x0C4, 0x077, 0x083, 0x063, 0x020, 0x088, 0x0F6,
    0x0AD, 0x0F3, 0x0E8, 0x04A, 0x0E9, 0x035, 0x01C, 0x05F, 0x019, 0x01F, 0x07D,
    0x070, 0x0FB, 0x0D1, 0x051, 0x010, 0x0D3, 0x02E, 0x061, 0x09D, 0x05C, 0x02A,
    0x042, 0x0BE, 0x0E6};

unsigned char Decode_table[256];
std::once_flag g_decode_once;

void buildDecodeTable() {
    unsigned char encode[256];
    for (int i = 0; i < 256; i++) encode[i] = Table_0[i] ^ 8;
    for (int i = 0; i < 256; i++) Decode_table[encode[i]] = (unsigned char)i;
}

// ---- in-memory, decoding cursor over a cell file ---------------------------
struct Reader {
    const unsigned char* p = nullptr;
    const unsigned char* end = nullptr;

    bool take(void* dst, int n) {
        if (n < 0 || p + n > end) return false;
        unsigned char* d = static_cast<unsigned char*>(dst);
        for (int i = 0; i < n; i++) d[i] = Decode_table[p[i]];
        p += n;
        return true;
    }
    bool u16(unsigned short* v) { return take(v, 2); }
    bool i32(int* v)            { return take(v, 4); }
    bool f64(double* v)         { return take(v, 8); }
    bool u8(unsigned char* v)   { return take(v, 1); }
    bool skip(int n) { if (n < 0 || p + n > end) return false; p += n; return true; }
};

struct cm93_point   { unsigned short x, y; };
struct cm93_point3d { unsigned short x, y, z; };
struct GeomDesc  { int n_points = 0; int pointsOffset = 0; };
struct VRecDesc  { int edgeIndex = 0; unsigned char segment_usage = 0; };

struct RawObject {
    unsigned char  otype = 0;
    unsigned char  geotype = 0;
    unsigned short nGeomElements = 0;
    int  vectorDescOffset = -1;   // area/line -> CellInfo::objVectorDesc
    int  point2dIndex     = -1;   // point     -> CellInfo::point2d
    int  point3dDescIndex = -1;   // sounding  -> CellInfo::point3dDesc
    int  attrOffset       = -1;   // -> CellInfo::attrBlock
    int  attrBytes        = 0;
    unsigned char nAttributes = 0;
};

struct CellInfo {
    double transform_x_rate = 0, transform_y_rate = 0;
    double transform_x_origin = 0, transform_y_origin = 0;
    double lon_min = 0, lat_min = 0, lon_max = 0, lat_max = 0;  // header bbox (deg)

    std::vector<RawObject>     objects;
    std::vector<cm93_point>    point2d;
    std::vector<cm93_point>    vectorPoints;
    std::vector<cm93_point3d>  point3d;
    std::vector<GeomDesc>      edgeDesc;
    std::vector<GeomDesc>      point3dDesc;
    std::vector<VRecDesc>      objVectorDesc;
    std::vector<unsigned char> attrBlock;

    int n_vector_records = 0;
};

bool readHeader(Reader& r, CellInfo& cib) {
    double lon_min, lat_min, lon_max, lat_max;
    double easting_min, northing_min, easting_max, northing_max;
    if (!r.f64(&lon_min) || !r.f64(&lat_min) || !r.f64(&lon_max) || !r.f64(&lat_max))
        return false;
    if (!r.f64(&easting_min) || !r.f64(&northing_min) ||
        !r.f64(&easting_max) || !r.f64(&northing_max))
        return false;

    unsigned short usn_vector_records = 0, usn_point3d_records = 0,
                   usn_point2d_records = 0, usn_feature_records = 0, u16tmp = 0;
    int n_vector_record_points = 0, m_46 = 0, m_4a = 0, m_50 = 0, m_54 = 0,
        m_60 = 0, m_64 = 0, m_nrel = 0, m_72 = 0, m_78 = 0, m_7c = 0, i32tmp = 0;

    if (!r.u16(&usn_vector_records)) return false;
    if (!r.i32(&n_vector_record_points)) return false;
    if (!r.i32(&m_46)) return false;
    if (!r.i32(&m_4a)) return false;
    if (!r.u16(&usn_point3d_records)) return false;
    if (!r.i32(&m_50)) return false;
    if (!r.i32(&m_54)) return false;
    if (!r.u16(&usn_point2d_records)) return false;
    if (!r.u16(&u16tmp)) return false;   // m_5a
    if (!r.u16(&u16tmp)) return false;   // m_5c
    if (!r.u16(&usn_feature_records)) return false;
    if (!r.i32(&m_60)) return false;
    if (!r.i32(&m_64)) return false;
    if (!r.u16(&u16tmp)) return false;   // m_68
    if (!r.u16(&u16tmp)) return false;   // m_6a
    if (!r.u16(&u16tmp)) return false;   // m_6c
    if (!r.i32(&m_nrel)) return false;
    if (!r.i32(&m_72)) return false;
    if (!r.u16(&u16tmp)) return false;   // m_76
    if (!r.i32(&m_78)) return false;
    if (!r.i32(&m_7c)) return false;
    (void)m_54; (void)m_60; (void)m_64; (void)m_72; (void)m_7c; (void)m_nrel;

    // Transform coefficients (cell-int -> Mercator metres on Intl-1924).
    double delta_x = easting_max - easting_min;
    if (delta_x < 0) delta_x += kCm93SemiMajor * 2.0 * kPI;  // one trip round
    cib.transform_x_rate = delta_x / 65535.0;
    cib.transform_y_rate = (northing_max - northing_min) / 65535.0;
    cib.transform_x_origin = easting_min;
    cib.transform_y_origin = northing_min;
    cib.lon_min = lon_min; cib.lat_min = lat_min;
    cib.lon_max = lon_max; cib.lat_max = lat_max;

    const int n_objvec = m_4a + m_46;
    // Reject implausible counts (corrupt / non-CM93 file).
    if (usn_vector_records < 0 || usn_vector_records > kMaxRecords ||
        n_vector_record_points < 0 || n_vector_record_points > kMaxRecords ||
        usn_point3d_records > kMaxRecords || m_50 < 0 || m_50 > kMaxRecords ||
        usn_point2d_records > kMaxRecords || usn_feature_records > kMaxRecords ||
        n_objvec < 0 || n_objvec > kMaxRecords || m_78 < 0 || m_78 > kMaxRecords)
        return false;

    cib.objects.assign(usn_feature_records, RawObject{});
    cib.point2d.assign(usn_point2d_records, cm93_point{0, 0});
    cib.objVectorDesc.assign(n_objvec, VRecDesc{});
    cib.attrBlock.assign(m_78, 0);
    cib.edgeDesc.assign(usn_vector_records, GeomDesc{});
    cib.vectorPoints.assign(n_vector_record_points, cm93_point{0, 0});
    cib.point3dDesc.assign(usn_point3d_records, GeomDesc{});
    cib.point3d.assign(m_50, cm93_point3d{0, 0, 0});
    cib.n_vector_records = usn_vector_records;
    return true;
}

bool readVectorTable(Reader& r, CellInfo& cib) {
    int off = 0;
    for (std::size_t e = 0; e < cib.edgeDesc.size(); ++e) {
        unsigned short np = 0;
        if (!r.u16(&np)) return false;
        if (off + (int)np > (int)cib.vectorPoints.size()) return false;
        cib.edgeDesc[e].n_points = np;
        cib.edgeDesc[e].pointsOffset = off;
        for (int i = 0; i < np; ++i) {
            unsigned short x, y;
            if (!r.u16(&x) || !r.u16(&y)) return false;
            cib.vectorPoints[off + i] = {x, y};
        }
        off += np;
    }
    return true;
}

bool read3dTable(Reader& r, CellInfo& cib) {
    int off = 0;
    for (std::size_t d = 0; d < cib.point3dDesc.size(); ++d) {
        unsigned short np = 0;
        if (!r.u16(&np)) return false;
        if (off + (int)np > (int)cib.point3d.size()) return false;
        cib.point3dDesc[d].n_points = np;
        cib.point3dDesc[d].pointsOffset = off;
        for (int i = 0; i < np; ++i) {
            unsigned short x, y, z;
            if (!r.u16(&x) || !r.u16(&y) || !r.u16(&z)) return false;
            cib.point3d[off + i] = {x, y, z};
        }
        off += np;   // advance by point count (OpenCPN advances by 1; fixed here)
    }
    return true;
}

bool read2dTable(Reader& r, CellInfo& cib) {
    for (std::size_t i = 0; i < cib.point2d.size(); ++i) {
        unsigned short x, y;
        if (!r.u16(&x) || !r.u16(&y)) return false;
        cib.point2d[i] = {x, y};
    }
    return true;
}

bool readFeatureTable(Reader& r, CellInfo& cib) {
    int vecOff = 0, attrOff = 0;
    for (std::size_t io = 0; io < cib.objects.size(); ++io) {
        RawObject& obj = cib.objects[io];
        unsigned char otype = 0, geom = 0;
        unsigned short objBytes = 0;
        if (!r.u8(&otype) || !r.u8(&geom) || !r.u16(&objBytes)) return false;
        obj.otype = otype;
        obj.geotype = geom;

        unsigned short nElem = 0, index = 0;
        switch (geom & 0x0f) {
            case 4:   // area
            case 2: { // line
                if (!r.u16(&nElem)) return false;
                obj.nGeomElements = nElem;
                objBytes -= (unsigned short)((nElem * 2) + 2);
                obj.vectorDescOffset = vecOff;
                for (int i = 0; i < nElem; ++i) {
                    if (!r.u16(&index)) return false;
                    const int e = index & 0x1fff;
                    if (e > cib.n_vector_records) return false;
                    if (vecOff >= (int)cib.objVectorDesc.size()) return false;
                    cib.objVectorDesc[vecOff].edgeIndex = e;
                    cib.objVectorDesc[vecOff].segment_usage = (unsigned char)(index >> 13);
                    ++vecOff;
                }
                break;
            }
            case 1: {  // point
                if (!r.u16(&index)) return false;
                objBytes -= 2;
                obj.nGeomElements = 1;
                obj.point2dIndex = index;
                break;
            }
            case 8: {  // sounding (3d multipoint)
                if (!r.u16(&index)) return false;
                objBytes -= 2;
                obj.nGeomElements = 1;
                obj.point3dDescIndex = index;
                break;
            }
            default: break;
        }

        if ((geom & 0x10) == 0x10) {        // related (children) — skip contents
            unsigned char nrel = 0;
            if (!r.u8(&nrel)) return false;
            objBytes -= (unsigned short)((nrel * 2) + 1);
            for (int j = 0; j < nrel; ++j) {
                unsigned short idx;
                if (!r.u16(&idx)) return false;
            }
        }
        if ((geom & 0x20) == 0x20) {        // related count
            unsigned short nrel;
            if (!r.u16(&nrel)) return false;
            objBytes -= 2;
        }
        // (geom & 0x40): nothing
        if ((geom & 0x80) == 0x80) {        // attributes
            unsigned char nattr = 0;
            if (!r.u8(&nattr)) return false;
            obj.nAttributes = nattr;
            objBytes -= 5;
            const int len = (int)(short)objBytes;   // remaining = attribute bytes
            if (len < 0 || attrOff + len > (int)cib.attrBlock.size()) return false;
            obj.attrOffset = attrOff;
            obj.attrBytes = len;
            if (len > 0 && !r.take(&cib.attrBlock[attrOff], len)) return false;
            attrOff += len;
        }
    }
    return true;
}

bool ingestCell(const QByteArray& buf, CellInfo& cib) {
    Reader r;
    r.p = reinterpret_cast<const unsigned char*>(buf.constData());
    r.end = r.p + buf.size();

    unsigned short word0 = 0;
    int int0 = 0, int1 = 0;
    if (!r.u16(&word0) || !r.i32(&int0) || !r.i32(&int1)) return false;
    if ((int)word0 + int0 + int1 != buf.size()) return false;   // corrupt
    if (!readHeader(r, cib)) return false;
    if (!readVectorTable(r, cib)) return false;
    if (!read3dTable(r, cib)) return false;
    if (!read2dTable(r, cib)) return false;
    if (!readFeatureTable(r, cib)) return false;
    return true;
}

// ---- geometry / attribute helpers ------------------------------------------
Cm93LL transformPoint(const CellInfo& cib, unsigned short x, unsigned short y,
                      double offx, double offy) {
    double valx = (x * cib.transform_x_rate) + cib.transform_x_origin - offx;
    double valy = (y * cib.transform_y_rate) + cib.transform_y_origin - offy;
    Cm93LL p;
    p.lat = (2.0 * std::atan(std::exp(valy / kCm93SemiMajor)) - kPI / 2.0) / kDegree;
    // Do NOT wrap longitude to [-180,180): the transform is linear in the cell's
    // integer X (the header's delta_x correction already makes a seam-crossing
    // cell continuous), so a raw lon keeps each cell's geometry contiguous. Per-
    // point wrapping would split a cell straddling the antimeridian into points
    // at +179 and -179, drawing the polygon as bands across the whole map and
    // leaving its real area unfilled. The host places each cell across the seam
    // via its own per-cell wrap offset (keyed on the cell-centre).
    p.lon = valx / (kDegree * kCm93SemiMajor);
    return p;
}

std::string fmtNum(double d) {
    if (d == std::floor(d) && std::fabs(d) < 1e15)
        return std::to_string((long long)d);
    char b[40];
    std::snprintf(b, sizeof b, "%g", d);
    return std::string(b);
}

// CM93 COLMAR colour code -> S-52 COLOUR value list (from OpenCPN's
// translate_colmar). CM93 carries buoy/light colour as a single COLMAR enum;
// the symbology engine expects S-52 COLOUR numbers (optionally a list).
std::string translateColmar(int v) {
    switch (v) {
        case 1:  return "4";       // green
        case 2:  return "2";       // black
        case 3:  return "3";       // red
        case 4:  return "6";       // yellow
        case 5:  return "1";       // white
        case 6:  return "11";      // orange
        case 7:  return "2,6";     // black/yellow
        case 8:  return "2,6,2";   // black/yellow/black
        case 9:  return "6,2";     // yellow/black
        case 10: return "6,2,6";   // yellow/black/yellow
        case 11: return "3,1";     // red/white
        case 12: return "4,3,4";   // green/red/green
        case 13: return "3,4,3";   // red/green/red
        case 14: return "2,3,2";   // black/red/black
        case 15: return "6,3,6";   // yellow/red/yellow
        case 16: return "4,3";     // green/red
        case 17: return "3,4";     // red/green
        case 18: return "4,1";     // green/white
        default: return "";
    }
}

// CM93 object-class acronym substitutions to S-57 equivalents (from OpenCPN).
std::string mapClassName(const std::string& s) {
    if (s == "ITDARE") return "DEPARE";
    if (s == "_m_sor") return "M_COVR";
    if (s == "SPOGRD") return "DMPGRD";
    if (s == "FSHHAV") return "FSHFAC";
    if (s == "OFSPRD") return "CTNARE";
    return s;
}

// Decode one object's attribute block into (acronym,value) pairs. Also reports
// the WGS84 offset attributes (_wgsox/_wgsoy) when present (M_COVR objects).
void decodeAttrs(const CellInfo& cib, const RawObject& obj,
                 const Cm93Dictionary& dict,
                 std::vector<std::pair<std::string, std::string>>& attrs,
                 double* offX, double* offY) {
    if (obj.attrOffset < 0 || obj.nAttributes == 0) return;
    const unsigned char* b = cib.attrBlock.data() + obj.attrOffset;
    const int n = obj.attrBytes;
    int c = 0;
    for (int k = 0; k < obj.nAttributes; ++k) {
        if (c >= n) break;
        const unsigned char iattr = b[c++];
        std::string name = dict.attrName(iattr);
        const char vt = dict.attrType(iattr);
        std::string val;
        switch (vt) {
            case 'B': if (c + 1 > n) return; val = std::to_string((int)b[c]); c += 1; break;
            case 'W': {
                if (c + 2 > n) return;
                unsigned short w = (unsigned short)(b[c] | (b[c + 1] << 8));
                val = fmtNum(w / 10.0); c += 2; break;
            }
            case 'G': { if (c + 4 > n) return; int v; std::memcpy(&v, b + c, 4); val = std::to_string(v); c += 4; break; }
            case 'I': { if (c + 2 > n) return; unsigned short w = (unsigned short)(b[c] | (b[c + 1] << 8)); val = std::to_string((int)w); c += 2; break; }
            case 'S': { std::string s; while (c < n && b[c]) s += (char)b[c++]; if (c < n) ++c; val = s; break; }
            case 'C': { if (c + 3 > n) return; c += 3; std::string s; while (c < n && b[c]) s += (char)b[c++]; if (c < n) ++c; val = s; break; }
            case 'L': {
                if (c + 1 > n) return;
                unsigned char nl = b[c++]; std::string s;
                for (int i = 0; i < nl && c < n; ++i) { if (i) s += ','; s += std::to_string((int)b[c++]); }
                val = s; break;
            }
            case 'R': {
                if (c + 4 > n) return;
                float f; std::memcpy(&f, b + c, 4); c += 4;
                val = fmtNum(f);
                if (name == "_wgsox" && offX) *offX = f;
                else if (name == "_wgsoy" && offY) *offY = f;
                break;
            }
            default: return;   // unknown type: can't determine length, stop
        }
        if (name == "COLMAR") {                   // map CM93 colour enum -> COLOUR
            val = translateColmar(std::atoi(val.c_str()));
            name = "COLOUR";
        }
        if (!name.empty() && name != "NULLNM" && name != "UnknownAttr")
            attrs.emplace_back(std::move(name), std::move(val));
    }
}

// Assemble one area object's rings (cell-int space), honouring segment direction
// and ring-closure, mirroring OpenCPN's BuildGeom area case.
void assembleArea(const CellInfo& cib, const RawObject& obj,
                  std::vector<std::vector<cm93_point>>& rings) {
    std::vector<cm93_point> ring;
    cm93_point start{0, 0};
    bool newRing = true;
    for (int i = 0; i < obj.nGeomElements; ++i) {
        const int vi = obj.vectorDescOffset + i;
        if (vi < 0 || vi >= (int)cib.objVectorDesc.size()) return;
        const VRecDesc& vd = cib.objVectorDesc[vi];
        if (vd.edgeIndex < 0 || vd.edgeIndex >= (int)cib.edgeDesc.size()) return;
        const GeomDesc& gd = cib.edgeDesc[vd.edgeIndex];
        const int np = gd.n_points;
        if (gd.pointsOffset < 0 || gd.pointsOffset + np > (int)cib.vectorPoints.size()) return;
        const cm93_point* seg = &cib.vectorPoints[gd.pointsOffset];
        const bool backwards = (vd.segment_usage & 4) == 4;

        std::vector<cm93_point> pts;
        pts.reserve(np);
        if (!backwards) for (int j = 0; j < np; ++j) pts.push_back(seg[j]);
        else            for (int j = np - 1; j >= 0; --j) pts.push_back(seg[j]);
        if (pts.empty()) continue;

        if (newRing) {
            newRing = false;
            ring.clear();
            start = pts.front();
            for (const cm93_point& p : pts) ring.push_back(p);
        } else {
            for (std::size_t k = 1; k < pts.size(); ++k) ring.push_back(pts[k]);
        }
        const cm93_point endp = pts.back();
        if (endp.x == start.x && endp.y == start.y) {
            if (ring.size() >= 3) rings.push_back(ring);
            newRing = true;
        }
    }
    if (!newRing && ring.size() >= 3) rings.push_back(ring);
}

void assembleLine(const CellInfo& cib, const RawObject& obj,
                  std::vector<cm93_point>& line) {
    for (int i = 0; i < obj.nGeomElements; ++i) {
        const int vi = obj.vectorDescOffset + i;
        if (vi < 0 || vi >= (int)cib.objVectorDesc.size()) return;
        const VRecDesc& vd = cib.objVectorDesc[vi];
        if (vd.edgeIndex < 0 || vd.edgeIndex >= (int)cib.edgeDesc.size()) return;
        const GeomDesc& gd = cib.edgeDesc[vd.edgeIndex];
        const int np = gd.n_points;
        if (gd.pointsOffset < 0 || gd.pointsOffset + np > (int)cib.vectorPoints.size()) return;
        const cm93_point* seg = &cib.vectorPoints[gd.pointsOffset];
        const bool backwards = (vd.segment_usage & 4) == 4;
        if (!backwards) for (int j = 0; j < np; ++j)       line.push_back(seg[j]);
        else            for (int j = np - 1; j >= 0; --j)  line.push_back(seg[j]);
    }
}

// One M_COVR coverage object: its outline(s) in raw cell-int space plus the
// per-object WGS84 datum offset that applies to features it contains.
struct McovrInfo {
    std::vector<std::vector<cm93_point>> rings;   // raw (cell-int)
    double offx = 0.0, offy = 0.0;
};

// Collect every M_COVR object's outline + offset from a decoded cell.
void extractMcovr(const CellInfo& cib, const Cm93Dictionary& dict,
                  std::vector<McovrInfo>& out) {
    for (const RawObject& obj : cib.objects) {
        if ((obj.geotype & 0x0f) != 4) continue;   // area geometry only
        if (mapClassName(dict.className(obj.otype)) != "M_COVR") continue;
        McovrInfo m;
        std::vector<std::pair<std::string, std::string>> attrs;
        decodeAttrs(cib, obj, dict, attrs, &m.offx, &m.offy);
        assembleArea(cib, obj, m.rings);
        if (!m.rings.empty()) out.push_back(std::move(m));
    }
}

// Even-odd point-in-polygon test (cell-int space) over a set of rings (outer +
// any holes), so a point inside a hole counts as outside.
bool pointInRings(unsigned short px, unsigned short py,
                  const std::vector<std::vector<cm93_point>>& rings) {
    bool inside = false;
    for (const std::vector<cm93_point>& ring : rings) {
        const std::size_t n = ring.size();
        if (n < 3) continue;
        for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
            const double xi = ring[i].x, yi = ring[i].y;
            const double xj = ring[j].x, yj = ring[j].y;
            if (((yi > py) != (yj > py)) &&
                (px < (xj - xi) * (double(py) - yi) / (yj - yi) + xi))
                inside = !inside;
        }
    }
    return inside;
}

// Choose the datum offset for a feature at raw point (px,py): the offset of the
// M_COVR whose outline contains it; else the first M_COVR carrying a non-zero
// offset; else none. Mirrors OpenCPN's FindM_COVROffset intent.
void pickOffset(unsigned short px, unsigned short py,
                const std::vector<McovrInfo>& mcovr, double& offx, double& offy) {
    for (const McovrInfo& m : mcovr)
        if (pointInRings(px, py, m.rings)) { offx = m.offx; offy = m.offy; return; }
    for (const McovrInfo& m : mcovr)
        if (m.offx != 0.0 || m.offy != 0.0) { offx = m.offx; offy = m.offy; return; }
    offx = 0.0; offy = 0.0;
}

// Skip CM93 coverage/metadata classes from rendered features (like the ENC
// loader's skipLayer): M_COVR/M_QUAL/M_NSYS etc. carry no drawable geometry.
bool isMetaClass(const std::string& cls) {
    return cls.size() >= 2 && cls[0] == 'M' && cls[1] == '_';
}

} // namespace

bool Cm93ReadCellExtent(const QString& path,
                        double& lonMin, double& latMin,
                        double& lonMax, double& latMax) {
    std::call_once(g_decode_once, buildDecodeTable);

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    // Only the prolog (10 bytes) + first 4 header doubles (32 bytes) are needed.
    const QByteArray head = f.read(10 + 32);
    if (head.size() < 10 + 32) return false;

    Reader r;
    r.p = reinterpret_cast<const unsigned char*>(head.constData());
    r.end = r.p + head.size();
    unsigned short word0; int int0, int1;
    if (!r.u16(&word0) || !r.i32(&int0) || !r.i32(&int1)) return false;
    double lon0, lat0, lon1, lat1;
    if (!r.f64(&lon0) || !r.f64(&lat0) || !r.f64(&lon1) || !r.f64(&lat1)) return false;

    if (!(std::isfinite(lon0) && std::isfinite(lat0) &&
          std::isfinite(lon1) && std::isfinite(lat1)))
        return false;
    if (lat0 < -90.0 || lat0 > 90.0 || lat1 < -90.0 || lat1 > 90.0) return false;

    // Keep raw (continuous) longitudes so the catalog bbox lives in the same
    // frame as the decoded geometry (see transformPoint). For a cell whose
    // header expresses the seam as lon_max < lon_min, add a turn to restore a
    // positive-width, continuous box.
    lonMin = lon0; latMin = lat0;
    lonMax = lon1; latMax = lat1;
    if (lonMax < lonMin) lonMax += 360.0;
    return true;
}

bool Cm93DecodeCell(const QString& path, const Cm93Dictionary& dict,
                    std::vector<Cm93Object>& out, QString& err) {
    std::call_once(g_decode_once, buildDecodeTable);

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { err = QStringLiteral("cannot open cell: ") + path; return false; }
    const QByteArray buf = f.readAll();
    if (buf.isEmpty()) { err = QStringLiteral("empty cell: ") + path; return false; }

    CellInfo cib;
    if (!ingestCell(buf, cib)) { err = QStringLiteral("not a valid CM93 cell: ") + path; return false; }

    // Pass 1: collect every M_COVR outline + its own WGS84 datum offset, so each
    // feature can take the offset of the coverage region that contains it.
    std::vector<McovrInfo> mcovr;
    extractMcovr(cib, dict, mcovr);

    // Pass 2: build features (M_COVR/meta classes are not drawn).
    out.clear();
    out.reserve(cib.objects.size());
    for (const RawObject& obj : cib.objects) {
        const int prim = obj.geotype & 0x0f;
        if (prim != 1 && prim != 2 && prim != 4 && prim != 8) continue;

        std::string cls = mapClassName(dict.className(obj.otype));
        if (cls == "Unknown" || isMetaClass(cls)) continue;

        Cm93Object co;
        co.objClass = cls;
        co.geomPrim = prim;
        decodeAttrs(cib, obj, dict, co.attrs, nullptr, nullptr);

        if (prim == 4) {                       // area
            std::vector<std::vector<cm93_point>> rings;
            assembleArea(cib, obj, rings);
            if (rings.empty()) continue;
            double offx = 0.0, offy = 0.0;
            pickOffset(rings[0][0].x, rings[0][0].y, mcovr, offx, offy);
            for (const auto& ring : rings) {
                std::vector<Cm93LL> r;
                r.reserve(ring.size());
                for (const cm93_point& p : ring) r.push_back(transformPoint(cib, p.x, p.y, offx, offy));
                if (r.size() >= 3) co.rings.push_back(std::move(r));
            }
            if (co.rings.empty()) continue;
        } else if (prim == 2) {                // line
            std::vector<cm93_point> line;
            assembleLine(cib, obj, line);
            if (line.size() < 2) continue;
            double offx = 0.0, offy = 0.0;
            pickOffset(line[0].x, line[0].y, mcovr, offx, offy);
            std::vector<Cm93LL> r;
            r.reserve(line.size());
            for (const cm93_point& p : line) r.push_back(transformPoint(cib, p.x, p.y, offx, offy));
            co.rings.push_back(std::move(r));
        } else if (prim == 1) {                // point
            if (obj.point2dIndex < 0 || obj.point2dIndex >= (int)cib.point2d.size()) continue;
            const cm93_point& p = cib.point2d[obj.point2dIndex];
            double offx = 0.0, offy = 0.0;
            pickOffset(p.x, p.y, mcovr, offx, offy);
            co.rings.push_back({ transformPoint(cib, p.x, p.y, offx, offy) });
        } else {                               // prim == 8: soundings
            if (obj.point3dDescIndex < 0 || obj.point3dDescIndex >= (int)cib.point3dDesc.size()) continue;
            const GeomDesc& gd = cib.point3dDesc[obj.point3dDescIndex];
            if (gd.pointsOffset < 0 || gd.pointsOffset + gd.n_points > (int)cib.point3d.size()) continue;
            double offx = 0.0, offy = 0.0;
            pickOffset(cib.point3d[gd.pointsOffset].x, cib.point3d[gd.pointsOffset].y, mcovr, offx, offy);
            for (int i = 0; i < gd.n_points; ++i) {
                const cm93_point3d& p = cib.point3d[gd.pointsOffset + i];
                Cm93LL ll = transformPoint(cib, p.x, p.y, offx, offy);
                // z magic number (OpenCPN): >= 12000 => (z-12000) metres, else z/10.
                const double depth = (p.z >= 12000) ? double(p.z - 12000) : p.z / 10.0;
                co.soundings.push_back({ ll.lat, ll.lon, depth });
            }
            if (co.soundings.empty()) continue;
        }
        out.push_back(std::move(co));
    }

    if (out.empty()) { err = QStringLiteral("no decodable objects in cell: ") + path; return false; }
    return true;
}

bool Cm93ReadCellFootprint(const QString& path, const Cm93Dictionary& dict,
                           double& lonMin, double& latMin,
                           double& lonMax, double& latMax,
                           std::vector<std::vector<Cm93LL>>& coverage, QString& err) {
    std::call_once(g_decode_once, buildDecodeTable);
    coverage.clear();

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { err = QStringLiteral("cannot open cell: ") + path; return false; }
    const QByteArray buf = f.readAll();
    if (buf.isEmpty()) { err = QStringLiteral("empty cell: ") + path; return false; }

    CellInfo cib;
    if (!ingestCell(buf, cib)) { err = QStringLiteral("not a valid CM93 cell: ") + path; return false; }

    // Footprint box from the header (continuous longitude; see Cm93ReadCellExtent).
    lonMin = cib.lon_min; latMin = cib.lat_min;
    lonMax = cib.lon_max; latMax = cib.lat_max;
    if (lonMax < lonMin) lonMax += 360.0;

    // Coverage = each M_COVR outline, transformed to lon/lat with its own offset.
    std::vector<McovrInfo> mcovr;
    extractMcovr(cib, dict, mcovr);
    for (const McovrInfo& m : mcovr) {
        for (const std::vector<cm93_point>& ring : m.rings) {
            std::vector<Cm93LL> r;
            r.reserve(ring.size());
            for (const cm93_point& p : ring)
                r.push_back(transformPoint(cib, p.x, p.y, m.offx, m.offy));
            if (r.size() >= 3) coverage.push_back(std::move(r));
        }
    }
    return true;
}
