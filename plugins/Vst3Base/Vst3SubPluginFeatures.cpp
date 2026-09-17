/*
 * Vst3SubPluginFeatures.cpp - sub plugin keys for discovered VST3 plugins
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

#include "Vst3SubPluginFeatures.h"

#include <QLabel>

#include "Vst3Manager.h"

namespace lmms
{

using vst3::Vst3Manager;


Vst3SubPluginFeatures::Vst3SubPluginFeatures(Plugin::Type type) :
	SubPluginFeatures(type)
{
}




static const vst3::Vst3ClassInfo* findClass(
	const Plugin::Descriptor::SubPluginFeatures::Key& k)
{
	return Vst3Manager::instance()->findByUid(
		k.attributes["uid"], k.attributes["file"]);
}




void Vst3SubPluginFeatures::fillDescriptionWidget(QWidget* parent, const Key* key) const
{
	const auto* info = findClass(*key);
	if (!info) { return; }

	(new QLabel(parent))->setText("<b>" + info->name + "</b>"
		+ (info->version.isEmpty() ? QString() : " v" + info->version));
	if (!info->vendor.isEmpty())
	{
		new QLabel(QObject::tr("Vendor: %1").arg(info->vendor), parent);
	}
	if (!info->subCategories.isEmpty())
	{
		new QLabel(QObject::tr("Category: %1").arg(info->subCategories), parent);
	}
	new QLabel(QObject::tr("File: %1").arg(info->modulePath), parent);
}




QString Vst3SubPluginFeatures::displayName(const Key& k) const
{
	const auto* info = findClass(k);
	return info ? info->name : k.name;
}




QString Vst3SubPluginFeatures::description(const Key& k) const
{
	if (k.attributes["uid"].isEmpty())
	{
		return QObject::tr("Empty VST3 host - load any VST3 plugin from a file of your choice");
	}
	const auto* info = findClass(k);
	if (!info) { return QObject::tr("VST3 plugin not found"); }
	QString result = info->name;
	if (!info->vendor.isEmpty()) { result += " " + QObject::tr("by %1").arg(info->vendor); }
	if (!info->subCategories.isEmpty()) { result += "\n" + info->subCategories; }
	result += "\n" + info->modulePath;
	return result;
}




void Vst3SubPluginFeatures::listSubPluginKeys(const Plugin::Descriptor* desc,
	KeyList& kl) const
{
	if (m_type == Plugin::Type::Instrument)
	{
		// an "empty" entry: drag it into the song editor and pick the
		// .vst3 file manually in the instrument view
		Key::AttributeMap attributes;
		attributes["uid"] = QString();
		attributes["file"] = QString();
		kl.push_back(Key(desc, QObject::tr("VST3 (load file)"), attributes));
	}

	for (const auto& info : Vst3Manager::instance()->classes())
	{
		const bool matches = m_type == Plugin::Type::Instrument
			? info.isInstrument : info.isFx;
		if (!matches) { continue; }

		Key::AttributeMap attributes;
		attributes["uid"] = info.uid;
		attributes["file"] = info.modulePath;
		kl.push_back(Key(desc, info.name, attributes));
	}
}


} // namespace lmms
