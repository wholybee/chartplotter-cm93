// SPDX-License-Identifier: GPL-2.0-or-later
//
// CM93 chart-source plugin for HMV Chartplotter.
// Copyright (C) 2026 Warren Holybee
// CM93 decoding derived from OpenCPN (cm93.cpp, Copyright (C) 2010
// David S. Register), GPL-2.0-or-later. See README.md / COPYING.
#pragma once
#include <QString>
#include <memory>
#include "plugin_api.hpp"

class Cm93ChartSource;

// CM93 plugin: registers a CM93 vector-chart backend (Cm93ChartSource) with the
// host on initialize() and tears it down on shutdown(). The host selects it
// automatically for chart folders that look like a CM93 dataset (see
// Cm93ChartSource::canHandle); ENC folders continue to use the built-in reader.
//
// Built as a separate DLL and loaded via QPluginLoader, exactly like the other
// plugins. Unlike them, this plugin is GPL-2.0-or-later because its decoding is
// derived from OpenCPN; the host application is unaffected (it links none of
// this code at build time).
class Cm93Plugin : public IPlugin {
public:
    Cm93Plugin();
    ~Cm93Plugin() override;

    QString name() const override { return QStringLiteral("CM93 Charts"); }
    QString version() const override { return QStringLiteral("0.1"); }
    void initialize(ICoreApi* core) override;
    void shutdown() override;

private:
    ICoreApi*                        core_ = nullptr;
    std::unique_ptr<Cm93ChartSource> source_;
};
