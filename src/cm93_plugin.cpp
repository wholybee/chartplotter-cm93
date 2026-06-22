// SPDX-License-Identifier: GPL-2.0-or-later
//
// CM93 chart-source plugin for HMV Chartplotter.
// Copyright (C) 2026 Warren Holybee
// CM93 decoding derived from OpenCPN (cm93.cpp, Copyright (C) 2010
// David S. Register), GPL-2.0-or-later. See README.md / COPYING.
#include "cm93_plugin.hpp"
#include "cm93_chart_source.hpp"

#include <QtGlobal>

Cm93Plugin::Cm93Plugin() = default;
Cm93Plugin::~Cm93Plugin() = default;   // out-of-line: Cm93ChartSource complete here

void Cm93Plugin::initialize(ICoreApi* core) {
    core_ = core;
    source_ = std::make_unique<Cm93ChartSource>();
    core_->registerChartSource(source_.get());
    qInfo("CM93 plugin: registered chart source (CM93 v2).");
}

void Cm93Plugin::shutdown() {
    // Unregister before the source object is destroyed so the host can drain any
    // in-flight cell loads that hold a pointer to it.
    if (core_ && source_) core_->unregisterChartSource(source_.get());
    source_.reset();
    core_ = nullptr;
}
