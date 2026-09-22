/*
 * AudioFileProcessor.cpp - instrument for using audio files
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

#include "AudioFileProcessorView.h"

#include "AudioFileProcessor.h"
#include "AudioFileProcessorWaveView.h"

#include <QPainter>

#include "AutomatableButton.h"
#include "ComboBox.h"
#include "Engine.h"
#include "LcdSpinBox.h"
#include "DataFile.h"
#include "FileDialog.h"
#include "FontHelper.h"
#include "PixmapButton.h"
#include "Song.h"
#include "StringPairDrag.h"
#include "Track.h"
#include "Clipboard.h"


namespace lmms
{

namespace gui
{

namespace
{

// The extra strip that holds the warp and crossfade controls
constexpr int StripTop = 160;
constexpr int StripHeight = 40;

//! Toggle drawn like the plugin's own buttons: black when off, lit blue when on
class AfpToggle : public AutomatableButton
{
public:
	AfpToggle(const QString& text, QWidget* parent) :
		AutomatableButton(parent, text)
	{
		setText(text);
		setCheckable(true);
		setCursor(Qt::PointingHandCursor);
		setFixedSize(40, 18);
		setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const bool on = model()->value();
		const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
		QLinearGradient fill(0, 0, 0, height());
		fill.setColorAt(0, on ? QColor(110, 190, 255) : QColor(28, 28, 30));
		fill.setColorAt(1, on ? QColor(53, 128, 220) : QColor(0, 0, 0));
		p.setPen(QPen(on ? QColor(53, 109, 168) : QColor(70, 70, 72), 1));
		p.setBrush(fill);
		p.drawRoundedRect(r, 3, 3);
		auto f = font();
		f.setBold(true);
		p.setFont(f);
		p.setPen(on ? QColor(255, 255, 255) : QColor(207, 244, 254));
		p.drawText(rect(), Qt::AlignCenter, text());
	}
};

} // namespace


AudioFileProcessorView::AudioFileProcessorView(Instrument* instrument,
							QWidget* parent) :
	InstrumentView(instrument, parent)
{
	setFixedSize(sizeHint());

	m_openAudioFileButton = new PixmapButton(this);
	m_openAudioFileButton->setCursor(Qt::PointingHandCursor);
	m_openAudioFileButton->move(227, 72);
	m_openAudioFileButton->setActiveGraphic(PLUGIN_NAME::getIconPixmap(
							"select_file"));
	m_openAudioFileButton->setInactiveGraphic(PLUGIN_NAME::getIconPixmap(
							"select_file"));
	connect(m_openAudioFileButton, SIGNAL(clicked()),
					this, SLOT(openAudioFile()));
	m_openAudioFileButton->setToolTip(tr("Open sample"));

	m_reverseButton = new PixmapButton(this);
	m_reverseButton->setCheckable(true);
	m_reverseButton->move(164, 105);
	m_reverseButton->setActiveGraphic(PLUGIN_NAME::getIconPixmap(
							"reverse_on"));
	m_reverseButton->setInactiveGraphic(PLUGIN_NAME::getIconPixmap(
							"reverse_off"));
	m_reverseButton->setToolTip(tr("Reverse sample"));

// loop button group

	auto m_loopOffButton = new PixmapButton(this);
	m_loopOffButton->setCheckable(true);
	m_loopOffButton->move(190, 105);
	m_loopOffButton->setActiveGraphic(PLUGIN_NAME::getIconPixmap(
							"loop_off_on"));
	m_loopOffButton->setInactiveGraphic(PLUGIN_NAME::getIconPixmap(
							"loop_off_off"));
	m_loopOffButton->setToolTip(tr("Disable loop"));

	auto m_loopOnButton = new PixmapButton(this);
	m_loopOnButton->setCheckable(true);
	m_loopOnButton->move(190, 124);
	m_loopOnButton->setActiveGraphic(PLUGIN_NAME::getIconPixmap(
							"loop_on_on"));
	m_loopOnButton->setInactiveGraphic(PLUGIN_NAME::getIconPixmap(
							"loop_on_off"));
	m_loopOnButton->setToolTip(tr("Enable loop"));

	auto m_loopPingPongButton = new PixmapButton(this);
	m_loopPingPongButton->setCheckable(true);
	m_loopPingPongButton->move(216, 124);
	m_loopPingPongButton->setActiveGraphic(PLUGIN_NAME::getIconPixmap(
							"loop_pingpong_on"));
	m_loopPingPongButton->setInactiveGraphic(PLUGIN_NAME::getIconPixmap(
							"loop_pingpong_off"));
	m_loopPingPongButton->setToolTip(tr("Enable ping-pong loop"));

	m_loopGroup = new AutomatableButtonGroup(this);
	m_loopGroup->addButton(m_loopOffButton);
	m_loopGroup->addButton(m_loopOnButton);
	m_loopGroup->addButton(m_loopPingPongButton);

	m_stutterButton = new PixmapButton(this);
	m_stutterButton->setCheckable(true);
	m_stutterButton->move(164, 124);
	m_stutterButton->setActiveGraphic(PLUGIN_NAME::getIconPixmap(
								"stutter_on"));
	m_stutterButton->setInactiveGraphic(PLUGIN_NAME::getIconPixmap(
								"stutter_off"));
	m_stutterButton->setToolTip(
		tr("Continue sample playback across notes"));

	m_ampKnob = new VolumeKnob(KnobType::Bright26, this);
	m_ampKnob->move(5, 108);
	m_ampKnob->setHintText(tr("Amplify:"), "%");

	m_startKnob = new AudioFileProcessorWaveView::knob(this);
	m_startKnob->move(50, 108);
	m_startKnob->setHintText(tr("Start point:"), "");

	m_endKnob = new AudioFileProcessorWaveView::knob(this);
	m_endKnob->move(130, 108);
	m_endKnob->setHintText(tr("End point:"), "");

	m_loopKnob = new AudioFileProcessorWaveView::knob(this);
	m_loopKnob->move(90, 108);
	m_loopKnob->setHintText(tr("Loopback point:"), "");

// interpolation selector
	m_interpBox = new ComboBox(this);
	m_interpBox->setGeometry(142, 62, 82, ComboBox::DEFAULT_HEIGHT);

// warp / crossfade strip
	m_warpButton = new AfpToggle(tr("WARP"), this);
	m_warpButton->move(12, StripTop + 11);
	m_warpButton->setToolTip(tr("Time-stretch the sample to the song tempo without changing its pitch"));

	m_tempoSpinBox = new LcdSpinBox(3, this, tr("Sample tempo"));
	m_tempoSpinBox->setLabel(tr("BPM"));
	m_tempoSpinBox->move(60, StripTop + 4);
	m_tempoSpinBox->setToolTip(tr("Original tempo of the sample (detected when it is loaded)"));

	m_crossfadeKnob = new Knob(KnobType::Bright26, tr("X-FADE"), SMALL_FONT_SIZE, this);
	m_crossfadeKnob->move(206, StripTop + 3);
	m_crossfadeKnob->setHintText(tr("Loop crossfade:"), " ms");
	m_crossfadeKnob->setToolTip(tr("Crossfade the loop end into the loop point to remove clicks"));

// wavegraph
	m_waveView = 0;
	newWaveView();

	connect(castModel<AudioFileProcessor>(), SIGNAL(isPlaying(lmms::f_cnt_t)),
			m_waveView, SLOT(isPlaying(lmms::f_cnt_t)));

	qRegisterMetaType<lmms::f_cnt_t>("lmms::f_cnt_t");

	setAcceptDrops(true);
}

void AudioFileProcessorView::dragEnterEvent(QDragEnterEvent* dee)
{
	// For mimeType() and MimeType enum class
	using namespace Clipboard;

	if (dee->mimeData()->hasFormat(mimeType(MimeType::StringPair)))
	{
		QString txt = dee->mimeData()->data(
						mimeType(MimeType::StringPair));
		if (txt.section(':', 0, 0) == QString("clip_%1").arg(
							static_cast<int>(Track::Type::Sample)))
		{
			dee->acceptProposedAction();
		}
		else if (txt.section(':', 0, 0) == "samplefile")
		{
			dee->acceptProposedAction();
		}
		else
		{
			dee->ignore();
		}
	}
	else
	{
		dee->ignore();
	}
}

void AudioFileProcessorView::newWaveView()
{
	if (m_waveView)
	{
		delete m_waveView;
		m_waveView = 0;
	}
	m_waveView = new AudioFileProcessorWaveView(this, 245, 75, &castModel<AudioFileProcessor>()->sample(),
		dynamic_cast<AudioFileProcessorWaveView::knob*>(m_startKnob),
		dynamic_cast<AudioFileProcessorWaveView::knob*>(m_endKnob),
		dynamic_cast<AudioFileProcessorWaveView::knob*>(m_loopKnob));
	m_waveView->move(2, 172 + StripHeight);
	
	m_waveView->show();
}

void AudioFileProcessorView::dropEvent(QDropEvent* de)
{
	const auto type = StringPairDrag::decodeKey(de);
	const auto value = StringPairDrag::decodeValue(de);

	if (type == "samplefile") { castModel<AudioFileProcessor>()->setAudioFile(value); }
	else if (type == QString("clip_%1").arg(static_cast<int>(Track::Type::Sample)))
	{
		DataFile dataFile(value.toUtf8());
		castModel<AudioFileProcessor>()->setAudioFile(dataFile.content().firstChild().toElement().attribute("src"));
	}
	else
	{
		de->ignore();
		return;
	}

	m_waveView->updateSampleRange();
	Engine::getSong()->setModified();
	de->accept();
}

void AudioFileProcessorView::paintEvent(QPaintEvent*)
{
	QPainter p(this);

	// The original artwork is split at the plain brushed-metal gap above the waveform
	// to make room for the warp strip
	static auto s_artwork = PLUGIN_NAME::getIconPixmap("artwork");
	p.drawPixmap(0, 0, s_artwork, 0, 0, 250, StripTop);
	for (int y = StripTop; y < StripTop + StripHeight; y += 8)
	{
		p.drawPixmap(0, y, s_artwork, 0, 152, 250, std::min(8, StripTop + StripHeight - y));
	}
	p.drawPixmap(0, StripTop + StripHeight, s_artwork, 0, StripTop, 250, 250 - StripTop);

	// Recessed panel matching the knob strip
	{
		p.save();
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF panel(4.5, StripTop + 2.5, 241, StripHeight - 5);
		QLinearGradient bg(0, panel.top(), 0, panel.bottom());
		bg.setColorAt(0, QColor(10, 10, 11));
		bg.setColorAt(1, QColor(24, 25, 27));
		p.setPen(QPen(QColor(70, 72, 76), 1));
		p.setBrush(bg);
		p.drawRoundedRect(panel, 4, 4);

		auto a = castModel<AudioFileProcessor>();
		const int songTempo = Engine::getSong()->getTempo();
		const float ratio = a->warpModel().value() ? a->sampleTempoModel().value() / static_cast<float>(songTempo) : 1.f;
		p.setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
		p.setPen(a->warpModel().value() ? QColor(207, 244, 254) : QColor(120, 124, 130));
		p.drawText(QRectF(104, StripTop + 6, 96, 14), Qt::AlignLeft | Qt::AlignVCenter,
			tr("SONG %1 BPM").arg(songTempo));
		p.drawText(QRectF(104, StripTop + 20, 96, 14), Qt::AlignLeft | Qt::AlignVCenter,
			a->warpModel().value() ? tr("LENGTH x%1").arg(ratio, 0, 'f', 2) : tr("WARP OFF"));
		p.restore();
	}

	auto a = castModel<AudioFileProcessor>();

	QString file_name = "";

	int idx = a->sample().sampleFile().length();

	p.setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));

	QFontMetrics fm(p.font());

	// simple algorithm for creating a text from the filename that
	// matches in the white rectangle
	while(idx > 0 &&
		fm.size(Qt::TextSingleLine, file_name + "...").width() < 210)
	{
		file_name = a->sample().sampleFile()[--idx] + file_name;
	}

	if (idx > 0)
	{
		file_name = "..." + file_name;
	}

	p.setPen(QColor(255, 255, 255));
	p.drawText(8, 99, file_name);
}

void AudioFileProcessorView::sampleUpdated()
{
	m_waveView->updateSampleRange();
	m_waveView->update();
	update();
}

void AudioFileProcessorView::openAudioFile()
{
	QString af = FileDialog::openAudioFile();
	if (af.isEmpty()) { return; }

	castModel<AudioFileProcessor>()->setAudioFile(af);
	Engine::getSong()->setModified();
	m_waveView->updateSampleRange();
}

void AudioFileProcessorView::modelChanged()
{
	auto a = castModel<AudioFileProcessor>();
	connect(a, &AudioFileProcessor::sampleUpdated, this, &AudioFileProcessorView::sampleUpdated);
	m_ampKnob->setModel(&a->ampModel());
	m_startKnob->setModel(&a->startPointModel());
	m_endKnob->setModel(&a->endPointModel());
	m_loopKnob->setModel(&a->loopPointModel());
	m_reverseButton->setModel(&a->reverseModel());
	m_loopGroup->setModel(&a->loopModel());
	m_stutterButton->setModel(&a->stutterModel());
	m_interpBox->setModel(&a->interpolationModel());
	m_warpButton->setModel(&a->warpModel());
	m_tempoSpinBox->setModel(&a->sampleTempoModel());
	m_crossfadeKnob->setModel(&a->crossfadeModel());
	for (Model* model : std::initializer_list<Model*>{&a->warpModel(), &a->sampleTempoModel()})
	{
		connect(model, &Model::dataChanged, this, qOverload<>(&QWidget::update));
	}
	connect(Engine::getSong(), &Song::tempoChanged, this, qOverload<>(&QWidget::update));
	sampleUpdated();
}

} // namespace gui

} // namespace lmms
