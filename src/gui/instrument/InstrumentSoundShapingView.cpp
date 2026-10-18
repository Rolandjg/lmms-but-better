/*
 * InstrumentSoundShapingView.cpp - view for InstrumentSoundShaping class
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "InstrumentSoundShapingView.h"

#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "EnvelopeAndLfoParameters.h"
#include "EnvelopeAndLfoView.h"
#include "ComboBox.h"
#include "FontHelper.h"
#include "InstrumentSoundShaping.h"
#include "Knob.h"
#include "ModernWidgets.h"


namespace lmms::gui
{

InstrumentSoundShapingView::InstrumentSoundShapingView(QWidget* parent) :
	QWidget(parent),
	ModelView(nullptr, this),
	m_targetModel(new IntModel(0, 0, 2, nullptr))
{
	QVBoxLayout* mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(5, 5, 5, 5);
	mainLayout->setSpacing(5);

	// What the envelope and LFO below modulate. A dot marks targets that are in use.
	m_targetSelector = new ModernSegmented(this);
	m_targetSelector->setModel(m_targetModel);
	m_targetSelector->setLabels({tr("VOLUME"), tr("CUTOFF"), tr("RESO")});
	m_targetSelector->setToolTips({
		tr("Envelope and LFO for the volume"),
		tr("Envelope and LFO for the filter cutoff (needs the filter)"),
		tr("Envelope and LFO for the filter resonance (needs the filter)")});
	m_targetSelector->setItemPainter([this](QPainter& p, const QRectF& r, int index, const QColor& color)
	{
		const QString text = index == 0 ? tr("VOLUME") : index == 1 ? tr("CUTOFF") : tr("RESO");
		const auto fm = p.fontMetrics();
		const qreal textWidth = fm.horizontalAdvance(text);
		const qreal x = r.center().x() - textWidth / 2;
		const qreal baseline = r.center().y() + fm.capHeight() / 2.0;
		p.setPen(color);
		p.drawText(QPointF(x, baseline), text);

		if (!m_ss) { return; }
		const auto& params = index == 0 ? m_ss->getVolumeParameters()
			: index == 1 ? m_ss->getCutoffParameters() : m_ss->getResonanceParameters();
		if (params.getAmountModel().value() != 0.f || params.getLfoAmountModel().value() != 0.f)
		{
			p.setRenderHint(QPainter::Antialiasing);
			p.setPen(Qt::NoPen);
			p.setBrush(modern::accentColor());
			p.drawEllipse(QPointF(x - 6, baseline - fm.capHeight() / 2.0), 2.2, 2.2);
		}
	}, 72);
	mainLayout->addWidget(m_targetSelector);

	m_targetStack = new QStackedWidget(this);
	m_volumeView = new EnvelopeAndLfoView(m_targetStack);
	m_cutoffView = new EnvelopeAndLfoView(m_targetStack);
	m_resonanceView = new EnvelopeAndLfoView(m_targetStack);
	m_targetStack->addWidget(m_volumeView);
	m_targetStack->addWidget(m_cutoffView);
	m_targetStack->addWidget(m_resonanceView);
	connect(m_targetModel, &IntModel::dataChanged, this,
		[this]() { m_targetStack->setCurrentIndex(m_targetModel->value()); });
	mainLayout->addWidget(m_targetStack, 1);


	// The filter that the cutoff and resonance targets act on
	m_filterRow = new QWidget(this);
	QGridLayout* filterLayout = new QGridLayout(m_filterRow);
	filterLayout->setContentsMargins(0, 2, 0, 0);
	filterLayout->setHorizontalSpacing(4);
	filterLayout->setVerticalSpacing(3);
	filterLayout->setColumnStretch(0, 1);

	m_filterToggle = new ModernHeader(tr("FILTER"), m_filterRow, true);
	m_filterToggle->setToolTip(tr("Switch the filter on or off"));
	filterLayout->addWidget(m_filterToggle, 0, 0);

	m_filterComboBox = new ComboBox(m_filterRow);
	filterLayout->addWidget(m_filterComboBox, 1, 0);

	m_filterCutKnob = new Knob(KnobType::Modern, tr("FREQ"), m_filterRow);
	m_filterCutKnob->setHintText(tr("Cutoff frequency:"), " " + tr("Hz"));
	filterLayout->addWidget(m_filterCutKnob, 0, 1, 2, 1, Qt::AlignBottom);

	m_filterResKnob = new Knob(KnobType::Modern, tr("RESO"), m_filterRow);
	m_filterResKnob->setHintText(tr("Q/Resonance:"), "");
	filterLayout->addWidget(m_filterResKnob, 0, 2, 2, 1, Qt::AlignBottom);

	mainLayout->addWidget(m_filterRow);


	m_singleStreamInfoLabel = new QLabel(tr("Envelopes, LFOs and filters are not supported by the current instrument."), this);
	m_singleStreamInfoLabel->setWordWrap(true);
	// TODO Could also be rendered in system font size...
	m_singleStreamInfoLabel->setFont(adjustedToPixelSize(m_singleStreamInfoLabel->font(), DEFAULT_FONT_SIZE));

	mainLayout->addWidget(m_singleStreamInfoLabel, 0, Qt::AlignTop);
}




InstrumentSoundShapingView::~InstrumentSoundShapingView()
{
	// The selector has to go before the model it shows
	delete m_targetSelector;
	delete m_targetModel;
}



void InstrumentSoundShapingView::setFunctionsHidden( bool hidden )
{
	m_targetSelector->setHidden( hidden );
	m_targetStack->setHidden( hidden );
	m_filterRow->setHidden( hidden );
	m_singleStreamInfoLabel->setHidden( !hidden );
}



void InstrumentSoundShapingView::modelChanged()
{
	if (m_ss)
	{
		for (auto params : {&m_ss->getVolumeParameters(), &m_ss->getCutoffParameters(), &m_ss->getResonanceParameters()})
		{
			params->getAmountModel().disconnect(m_targetSelector);
			params->getLfoAmountModel().disconnect(m_targetSelector);
		}
	}

	m_ss = castModel<InstrumentSoundShaping>();
	m_filterToggle->setModel(&m_ss->getFilterEnabledModel());
	m_filterComboBox->setModel(&m_ss->getFilterModel());
	m_filterCutKnob->setModel(&m_ss->getFilterCutModel());
	m_filterResKnob->setModel(&m_ss->getFilterResModel());

	m_volumeView->setModel(&m_ss->getVolumeParameters());
	m_cutoffView->setModel(&m_ss->getCutoffParameters());
	m_resonanceView->setModel(&m_ss->getResonanceParameters());

	// Keep the "in use" dots of the target selector current
	for (auto params : {&m_ss->getVolumeParameters(), &m_ss->getCutoffParameters(), &m_ss->getResonanceParameters()})
	{
		connect(&params->getAmountModel(), &FloatModel::dataChanged, m_targetSelector, qOverload<>(&QWidget::update));
		connect(&params->getLfoAmountModel(), &FloatModel::dataChanged, m_targetSelector, qOverload<>(&QWidget::update));
	}
	m_targetSelector->update();
}





} // namespace lmms::gui
