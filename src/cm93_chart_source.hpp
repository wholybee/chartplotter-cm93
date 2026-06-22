// SPDX-License-Identifier: GPL-2.0-or-later
//
// CM93 chart-source plugin for HMV Chartplotter.
// Copyright (C) 2026 Warren Holybee
//
// The CM93 binary decoding logic in this plugin is derived from OpenCPN
// (gui/src/cm93.cpp, Copyright (C) 2010 David S. Register), which is licensed
// GPL-2.0-or-later. This plugin therefore inherits that licence. It is built and
// distributed as a standalone, dynamically-loaded module; the host application
// remains under its own (LGPL-2.1) licence and links to none of this code at
// build time. See plugins/cm93_plugin/README.md.
#pragma once
#include <QString>
#include <mutex>
#include <vector>
#include "chart_source.hpp"      // IChartSource, ChartSourceCell (host SDK)
#include "cm93_dictionary.hpp"   // Cm93Dictionary

// CM93 (C-Map CM93 v2) backend. Implements the host's IChartSource so CM93
// vector cells flow through the same catalog / quilt / cache / symbology
// pipeline as ENC charts. catalog() enumerates cell files and reads each cell's
// footprint from its header; loadCell() decodes a cell and translates its
// objects to S-57 Features. The heavy lifting lives in cm93_decoder /
// cm93_dictionary (ported from OpenCPN's GPL cm93.cpp).
class Cm93ChartSource : public IChartSource {
public:
    Cm93ChartSource() = default;
    ~Cm93ChartSource() override = default;

    QString sourceId() const override { return QStringLiteral("cm93"); }
    QString displayName() const override { return QStringLiteral("CM93"); }

    bool canHandle(const QString& root) const override;

    bool catalog(const QString& root,
                 std::vector<ChartSourceCell>& out, QString& errMsg,
                 const std::function<void(int, int)>& progress) override;

    bool loadCell(const QString& cellId,
                  std::vector<Feature>& out, BBox& bbox,
                  QString& errMsg) override;

private:
    // Load the CM93 dictionary once (thread-safe). `nearPath` is any path inside
    // the dataset (the root, or a cell file) used to locate the .DIC files.
    bool ensureDict(const QString& nearPath, QString& errMsg);

    mutable std::mutex mutex_;     // guards the lazy dictionary load
    Cm93Dictionary     dict_;
    bool               dictLoaded_ = false;
};
