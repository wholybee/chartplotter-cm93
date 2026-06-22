// SPDX-License-Identifier: GPL-2.0-or-later
//
// CM93 chart-source plugin for HMV Chartplotter.
// Copyright (C) 2026 Warren Holybee. GPL-2.0-or-later. See README.md / COPYING.
#include "cm93_plugin.hpp"
#include "plugin_factory.hpp"

#include <QObject>

// QPluginLoader entry point for the CM93 plugin DLL. Mirrors the other plugins'
// factories: the only QObject the host instantiates; it validates ABI/metadata
// before handing back the real IPlugin.
class Cm93PluginFactory : public QObject, public IPluginFactory {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID CHARTPLOTTER_PLUGIN_IID FILE "cm93_plugin.json")
    Q_INTERFACES(IPluginFactory)
public:
    int abiVersion() const override { return kPluginAbiVersion; }
    std::unique_ptr<IPlugin> create() override {
        return std::make_unique<Cm93Plugin>();
    }
};

#include "cm93_plugin_factory.moc"
