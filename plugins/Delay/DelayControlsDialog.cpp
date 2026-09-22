/*
 * DelayControlsDialog.cpp - definition of DelayControlsDialog class.
 *
 * Copyright (c) 2014 David French <dave/dot/french3/at/googlemail/dot/com>
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

#include "DelayControlsDialog.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <cmath>

#include "DelayControls.h"
#include "DelayEffect.h"
#include "FontHelper.h"
#include "ModernWidgets.h"
#include "TempoSyncKnob.h"

namespace lmms::gui
{

namespace
{
const QColor LeftColor = modern::accentColor();
const QColor RightColor = modern::secondaryColor();
}


DelayControlsDialog::DelayControlsDialog( DelayControls *controls ) :
	EffectControlDialog( controls )
{
	modern::applyWindowStyle(this);

	auto root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(6);
	root->setSizeConstraint(QLayout::SetFixedSize);

	// Display + output meter
	auto top = new QHBoxLayout();
	top->setSpacing(6);
	top->addWidget(new DelayTapDisplay(controls, this), 1);
	top->addWidget(new ModernMeter(this, &controls->m_outPeakL, &controls->m_outPeakR));
	root->addLayout(top);

	auto makeSyncKnob = [this](TempoSyncKnobModel* model, const QString& label, const QString& hint)
	{
		auto knob = new TempoSyncKnob(KnobType::Modern, label, this);
		knob->setModel(model);
		knob->setHintText(hint, " s");
		return knob;
	};

	auto sections = new QGridLayout();
	sections->setSpacing(6);

	// Time
	auto time = new ModernSection(tr("Time"), this);
	time->grid()->addWidget(makeSyncKnob(&controls->m_delayTimeModel, tr("TIME"), tr("Delay time:")), 0, 0);
	time->grid()->addWidget(modern::makeKnob(this, &controls->m_offsetModel, tr("OFFSET"),
		tr("Right channel time offset:"), "%"), 0, 1);
	auto mode = new ModernSegmented(this);
	mode->setModel(&controls->m_modeModel);
	time->grid()->addWidget(mode, 1, 0, 1, 2);
	sections->addWidget(time, 0, 0);

	// Modulation
	auto mod = new ModernSection(tr("Modulation"), this);
	mod->grid()->addWidget(makeSyncKnob(&controls->m_lfoTimeModel, tr("RATE"), tr("LFO period:")), 0, 0);
	mod->grid()->addWidget(makeSyncKnob(&controls->m_lfoAmountModel, tr("DEPTH"), tr("LFO depth:")), 0, 1);
	auto freeze = new ModernToggle(tr("FREEZE"), this);
	freeze->setModel(&controls->m_freezeModel);
	freeze->setToolTip(tr("Hold the current echoes forever and ignore new input"));
	mod->grid()->addWidget(freeze, 1, 0, 1, 2);
	sections->addWidget(mod, 0, 1);

	// Feedback
	auto fb = new ModernSection(tr("Feedback"), this);
	fb->grid()->addWidget(modern::makeKnob(this, &controls->m_feedbackModel, tr("AMOUNT"),
		tr("Feedback:"), ""), 0, 0);
	fb->grid()->addWidget(modern::makeKnob(this, &controls->m_lowCutModel, tr("LO CUT"),
		tr("Feedback low cut:"), " Hz"), 0, 1);
	fb->grid()->addWidget(modern::makeKnob(this, &controls->m_highCutModel, tr("HI CUT"),
		tr("Feedback high cut:"), " Hz"), 0, 2);
	fb->grid()->addWidget(modern::makeKnob(this, &controls->m_driveModel, tr("DRIVE"),
		tr("Feedback saturation:"), "%"), 0, 3);
	sections->addWidget(fb, 1, 0);

	// Output
	auto out = new ModernSection(tr("Output"), this);
	out->grid()->addWidget(modern::makeKnob(this, &controls->m_widthModel, tr("WIDTH"),
		tr("Stereo width:"), "%"), 0, 0);
	out->grid()->addWidget(modern::makeKnob(this, &controls->m_duckModel, tr("DUCK"),
		tr("Duck echoes while input plays:"), "%"), 0, 1);
	out->grid()->addWidget(modern::makeKnob(this, &controls->m_outGainModel, tr("GAIN"),
		tr("Output gain:"), " dB"), 0, 2);
	sections->addWidget(out, 1, 1);

	root->addLayout(sections);
}




DelayTapDisplay::DelayTapDisplay(DelayControls* controls, QWidget* parent) :
	QWidget(parent),
	m_controls(controls)
{
	setMinimumSize(sizeHint());
	setCursor(Qt::SizeAllCursor);
	setToolTip(tr("Drag horizontally to change the delay time, vertically to change the feedback"));

	for (Model* m : std::initializer_list<Model*>{&controls->m_delayTimeModel, &controls->m_feedbackModel,
		&controls->m_offsetModel, &controls->m_modeModel, &controls->m_highCutModel, &controls->m_lowCutModel,
		&controls->m_freezeModel, &controls->m_widthModel})
	{
		connect(m, &Model::dataChanged, this, qOverload<>(&QWidget::update));
	}
}




void DelayTapDisplay::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	const QRectF area = QRectF(rect());
	modern::paintDisplay(p, area, 8, 2);
	p.setRenderHint(QPainter::Antialiasing);

	const auto mode = static_cast<DelayControls::Mode>(m_controls->m_modeModel.value());
	const float timeL = m_controls->m_delayTimeModel.value();
	const float timeR = mode == DelayControls::Mode::Mono
		? timeL : timeL * (1.f + m_controls->m_offsetModel.value() * 0.01f);
	const bool frozen = m_controls->m_freezeModel.value();
	const float feedback = frozen ? 1.f : m_controls->m_feedbackModel.value();

	// Rough per-repeat loss from the feedback filters, so darker settings visibly decay faster
	const float hiCut = m_controls->m_highCutModel.value();
	const float loCut = m_controls->m_lowCutModel.value();
	const float filterLoss = frozen ? 1.f
		: std::clamp(1.f - 0.25f * std::log2(20000.f / hiCut) / 5.3f - 0.25f * std::log2(loCut / 20.f) / 6.6f, 0.4f, 1.f);

	struct Tap { float time; float level; int channel; };
	std::vector<Tap> taps;
	const float window = std::clamp(std::max(timeL, timeR) * 6.5f, 0.5f, DelayEffect::MaxDelaySeconds);
	float level = 1.f;
	switch (mode)
	{
	case DelayControls::Mode::Stereo:
	case DelayControls::Mode::Mono:
		for (int ch = 0; ch < 2; ++ch)
		{
			const float t = ch == 0 ? timeL : timeR;
			level = 1.f;
			for (float at = t; at <= window && level > 0.01f; at += t)
			{
				taps.push_back({at, level, ch});
				level *= feedback * filterLoss;
			}
		}
		break;
	case DelayControls::Mode::PingPong:
	{
		float at = timeL;
		int ch = 0;
		while (at <= window && level > 0.01f)
		{
			taps.push_back({at, level, ch});
			level *= feedback * filterLoss;
			ch = 1 - ch;
			at += ch == 0 ? timeL : timeR;
		}
		break;
	}
	}

	const double mid = area.center().y();
	const double half = area.height() / 2.0 - 10.0;
	const double left = 10.0;
	const double usable = area.width() - 20.0;

	// Dry impulse at time zero
	p.setPen(QPen(QColor(255, 255, 255, 120), 2, Qt::SolidLine, Qt::RoundCap));
	p.drawLine(QPointF(left, mid - half), QPointF(left, mid + half));

	for (const auto& tap : taps)
	{
		const double x = left + usable * tap.time / window;
		const double h = half * tap.level;
		QColor color = tap.channel == 0 ? LeftColor : RightColor;
		color.setAlphaF(0.35f + 0.65f * tap.level);
		p.setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap));
		if (mode == DelayControls::Mode::Mono) { p.drawLine(QPointF(x, mid - h), QPointF(x, mid + h)); }
		else if (tap.channel == 0) { p.drawLine(QPointF(x, mid - 1), QPointF(x, mid - h)); }
		else { p.drawLine(QPointF(x, mid + 1), QPointF(x, mid + h)); }
	}

	p.setPen(QPen(QColor(255, 255, 255, 40), 1));
	p.drawLine(QPointF(left, mid), QPointF(area.right() - 10, mid));

	p.setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	p.setPen(modern::dimTextColor());
	if (mode != DelayControls::Mode::Mono)
	{
		p.drawText(QPointF(area.right() - 18, 14), "L");
		p.drawText(QPointF(area.right() - 18, area.bottom() - 6), "R");
	}
	p.drawText(QRectF(left + 6, area.bottom() - 18, 150, 14), Qt::AlignLeft | Qt::AlignVCenter,
		tr("%1 ms  ·  window %2 s").arg(timeL * 1000.f, 0, 'f', 1).arg(window, 0, 'f', 2));

	if (frozen)
	{
		p.setPen(Qt::NoPen);
		p.setBrush(modern::warningColor());
		const QRectF badge(area.right() - 76, 6, 48, 16);
		p.drawRoundedRect(badge, 8, 8);
		p.setPen(QColor(16, 18, 20));
		p.drawText(badge, Qt::AlignCenter, tr("FROZEN"));
	}
}




void DelayTapDisplay::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton) { return; }
	m_dragging = true;
	m_lastPos = event->pos();
	m_controls->m_delayTimeModel.addJournalCheckPoint();
	m_controls->m_feedbackModel.addJournalCheckPoint();
}




void DelayTapDisplay::mouseMoveEvent(QMouseEvent* event)
{
	if (!m_dragging) { return; }
	const QPoint delta = event->pos() - m_lastPos;
	m_lastPos = event->pos();

	// Relative, multiplicative time changes feel natural over the whole 10 ms - 5 s range
	auto& time = m_controls->m_delayTimeModel;
	const float fine = (event->modifiers() & Qt::ShiftModifier) ? 0.2f : 1.f;
	time.setValue(time.value() * std::exp(delta.x() * 0.006f * fine));

	auto& fb = m_controls->m_feedbackModel;
	fb.setValue(fb.value() - delta.y() * fine / static_cast<float>(height()));
}




void DelayTapDisplay::mouseReleaseEvent(QMouseEvent*)
{
	m_dragging = false;
}


} // namespace lmms::gui
