/*
 * Vst3Effect.h - effect plugin hosting VST3 effects
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

#ifndef LMMS_VST3_EFFECT_H
#define LMMS_VST3_EFFECT_H

#include <memory>
#include <vector>

#include "Effect.h"
#include "EffectControls.h"
#include "EffectControlDialog.h"
#include "Vst3Plugin.h"

namespace lmms
{

class Vst3Effect;

namespace gui
{
class Vst3FxControlDialog;
class Vst3PluginWidget;
}


class Vst3FxControls : public EffectControls
{
	Q_OBJECT

public:
	Vst3FxControls(Vst3Effect* effect, const QString& uid, const QString& file);

	void saveSettings(QDomDocument& doc, QDomElement& elem) override;
	void loadSettings(const QDomElement& elem) override;
	QString nodeName() const override { return "vst3controls"; }

	int controlCount() override;
	gui::EffectControlDialog* createView() override;

	vst3::Vst3Plugin* plugin() { return m_plugin.get(); }

private:
	std::unique_ptr<vst3::Vst3Plugin> m_plugin;

	friend class Vst3Effect;
	friend class gui::Vst3FxControlDialog;
};


class Vst3Effect : public Effect
{
	Q_OBJECT

public:
	Vst3Effect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);

	EffectControls* controls() override { return &m_controls; }

protected:
	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override;

private slots:
	void onSampleRateChanged();

private:
	Vst3FxControls m_controls;
	std::vector<SampleFrame> m_tmpOutput;
};


namespace gui
{

class Vst3FxControlDialog : public EffectControlDialog
{
	Q_OBJECT

public:
	explicit Vst3FxControlDialog(Vst3FxControls* controls);

private:
	Vst3PluginWidget* m_widget = nullptr;
};

} // namespace gui


} // namespace lmms

#endif // LMMS_VST3_EFFECT_H
