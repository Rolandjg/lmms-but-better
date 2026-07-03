/*
 * Vst3SubPluginFeatures.h - sub plugin keys for discovered VST3 plugins
 *
 * Copyright (c) 2026 LMMS developers
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#ifndef LMMS_VST3_SUB_PLUGIN_FEATURES_H
#define LMMS_VST3_SUB_PLUGIN_FEATURES_H

#include "Plugin.h"

#include "vst3base_export.h"

namespace lmms
{

class VST3BASE_EXPORT Vst3SubPluginFeatures : public Plugin::Descriptor::SubPluginFeatures
{
public:
	explicit Vst3SubPluginFeatures(Plugin::Type type);

	void fillDescriptionWidget(QWidget* parent, const Key* key) const override;
	void listSubPluginKeys(const Plugin::Descriptor* desc, KeyList& kl) const override;

private:
	QString displayName(const Key& k) const override;
	QString description(const Key& k) const override;
};

} // namespace lmms

#endif // LMMS_VST3_SUB_PLUGIN_FEATURES_H
