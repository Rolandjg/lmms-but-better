/*
 * FormantShifterControlDialog.cpp - editor for the formant shifter / vowel filter
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

#include "FormantShifterControlDialog.h"

#include <cmath>

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVBoxLayout>

#include <fftw3.h>

#include "AudioEngine.h"
#include "CaptionMenu.h"
#include "ComboBox.h"
#include "DeprecationHelper.h"
#include "Engine.h"
#include "FontHelper.h"
#include "FormantDsp.h"
#include "FormantShifterControls.h"
#include "Knob.h"
#include "LeftRightNav.h"
#include "ModernWidgets.h"

namespace lmms::gui
{

namespace
{

constexpr float MinDb = -96.f;
constexpr float MaxDb = 0.f;
constexpr float MinFreq = 20.f;
constexpr float MaxFreq = 20000.f;
constexpr int AnalysisSize = static_cast<int>(ScopeBuffer::Size);
const char* const VowelLetters[] = {"A", "E", "I", "O", "U"};

//! A column of knobs under a flat section title
QWidget* makeGroup(QWidget* parent, const QString& title, std::initializer_list<QWidget*> widgets)
{
	auto group = new QWidget(parent);
	auto layout = new QVBoxLayout(group);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(4);
	layout->addWidget(new ModernHeader(title, group));
	auto row = new QHBoxLayout();
	row->setSpacing(6);
	for (auto w : widgets) { row->addWidget(w, 0, Qt::AlignBottom); }
	row->addStretch(1);
	layout->addLayout(row);
	return group;
}

void paintLfoShape(QPainter& p, const QRectF& r, int index, const QColor& color)
{
	QPainterPath path;
	constexpr int Steps = 48;
	for (int i = 0; i <= Steps; ++i)
	{
		const float t = static_cast<float>(i) / Steps;
		// Random shapes get a fixed, recognisable pattern
		float v = 0.f;
		if (index == static_cast<int>(formant::LfoShape::SampleHold))
		{
			constexpr float Steps4[] = {0.6f, -0.4f, 0.9f, -0.8f};
			v = Steps4[std::min(static_cast<int>(t * 4), 3)];
		}
		else if (index == static_cast<int>(formant::LfoShape::SmoothRandom))
		{
			v = 0.7f * std::sin(t * 7.f) * std::cos(t * 3.f + 1.f) + 0.2f * std::sin(t * 13.f);
		}
		else { v = formant::lfoValue(static_cast<formant::LfoShape>(index), t); }
		const QPointF pt(r.left() + t * r.width(), r.center().y() - v * r.height() * 0.5);
		if (i == 0) { path.moveTo(pt); } else { path.lineTo(pt); }
	}
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(QPen(color, 1.3));
	p.setBrush(Qt::NoBrush);
	p.drawPath(path);
}

} // namespace


FormantShifterControlDialog::FormantShifterControlDialog(FormantShifterControls* controls) :
	EffectControlDialog(controls)
{
	modern::applyWindowStyle(this);
	auto c = controls;

	auto root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(6);
	root->setSizeConstraint(QLayout::SetFixedSize);

	// Presets and engine
	auto top = new QHBoxLayout();
	top->setSpacing(6);
	auto preset = new ComboBox(this);
	preset->setModel(&c->m_presetModel);
	preset->setFixedWidth(190);
	preset->setToolTip(tr("Presets set every control below; everything stays editable and automatable"));
	top->addWidget(preset);
	auto nav = new LeftRightNav(this);
	nav->setToolTip(tr("Previous / next preset"));
	connect(nav, &LeftRightNav::onNavLeft, this, [c] {
		const int n = c->m_presetModel.size();
		c->m_presetModel.setValue((c->m_presetModel.value() + n - 1) % n);
	});
	connect(nav, &LeftRightNav::onNavRight, this, [c] {
		c->m_presetModel.setValue((c->m_presetModel.value() + 1) % c->m_presetModel.size());
	});
	top->addWidget(nav);
	top->addStretch(1);
	auto mode = new ModernSegmented(this);
	mode->setModel(&c->m_modeModel);
	mode->setLabels({tr("SPECTRAL"), tr("RESONANT")});
	mode->setToolTips({
		tr("Spectral: moves the sound's own formants, pitch-shifts with formants kept, and imprints vowels "
			"(talkbox). Adds about 43 ms of latency, which LMMS compensates."),
		tr("Resonant: the classic formant filter, five resonant band-passes tuned to the vowel. No latency.")});
	top->addWidget(mode);
	root->addLayout(top);

	auto displayRow = new QHBoxLayout();
	displayRow->setSpacing(6);
	displayRow->addWidget(new FormantDisplay(c, this), 1);
	displayRow->addWidget(new ModernMeter(this, &c->m_outPeakL, &c->m_outPeakR));
	root->addLayout(displayRow);

	auto vowelRow = new QHBoxLayout();
	vowelRow->setSpacing(6);
	vowelRow->addWidget(new VowelStrip(c, this), 1);
	auto voice = new ModernSegmented(this);
	voice->setModel(&c->m_voiceModel);
	voice->setLabels({tr("BASS"), tr("TENOR"), tr("C-TENOR"), tr("ALTO"), tr("SOPRANO")});
	voice->setToolTip(tr("Whose vowels: sung formant tables from bass to soprano"));
	vowelRow->addWidget(voice);
	root->addLayout(vowelRow);

	// Shift / vowel / output
	auto formantKnob = modern::makeKnob(this, &c->m_formantModel, tr("FORMANT"), tr("Formant shift:"), " st");
	auto pitchKnob = modern::makeKnob(this, &c->m_pitchModel, tr("PITCH"), tr("Pitch shift:"), " st");
	auto fineKnob = modern::makeKnob(this, &c->m_fineModel, tr("FINE"), tr("Fine tune:"), " cents");
	auto spreadKnob = modern::makeKnob(this, &c->m_spreadModel, tr("SPREAD"),
		tr("Stereo spread (left formants down, right up):"), " st");
	auto preserve = new ModernToggle(tr("KEEP FORMANTS"), this);
	preserve->setModel(&c->m_preserveModel);
	preserve->setToolTip(tr("On: pitch shifting keeps the voice's character. Off: formants move with the pitch "
		"(chipmunk / slowed-down tape)"));
	auto shiftGroup = makeGroup(this, tr("SHIFT"), {formantKnob, pitchKnob, fineKnob, spreadKnob, preserve});

	auto imprintKnob = modern::makeKnob(this, &c->m_imprintModel, tr("IMPRINT"),
		tr("Vowel imprint (talkbox):"), "%");
	imprintKnob->setToolTip(tr("How much the vowel replaces the sound's own formants"));
	auto resKnob = modern::makeKnob(this, &c->m_resonanceModel, tr("RES"), tr("Resonance:"), "x");
	auto driveKnob = modern::makeKnob(this, &c->m_driveModel, tr("DRIVE"), tr("Drive:"), "%");
	driveKnob->setToolTip(tr("Saturates the input so there are more harmonics for the vowels to shape"));
	auto vowelGroup = makeGroup(this, tr("VOWEL"), {imprintKnob, resKnob, driveKnob});

	auto outputGroup = makeGroup(this, tr("OUTPUT"), {
		modern::makeKnob(this, &c->m_mixModel, tr("MIX"), tr("Mix:"), "%"),
		modern::makeKnob(this, &c->m_gainModel, tr("GAIN"), tr("Output gain:"), " dB")});

	auto sections = new QHBoxLayout();
	sections->setSpacing(14);
	sections->addWidget(shiftGroup);
	sections->addWidget(vowelGroup);
	sections->addWidget(outputGroup);
	root->addLayout(sections);

	// Modulation
	auto shape = new ModernSegmented(this);
	shape->setModel(&c->m_lfoShapeModel);
	shape->setItemPainter(paintLfoShape, 26);
	shape->setToolTips({tr("Sine"), tr("Triangle"), tr("Saw up"), tr("Saw down"), tr("Square"),
		tr("Sample & hold: a new random value every cycle"), tr("Smooth random")});
	auto sync = new ComboBox(this);
	sync->setModel(&c->m_lfoSyncModel);
	sync->setFixedWidth(78);
	sync->setToolTip(tr("Tempo sync: synced LFOs follow the song position, so they land on the beat"));
	auto shapeColumn = new QWidget(this);
	auto shapeLayout = new QVBoxLayout(shapeColumn);
	shapeLayout->setContentsMargins(0, 0, 0, 0);
	shapeLayout->setSpacing(4);
	shapeLayout->addWidget(shape);
	shapeLayout->addWidget(sync, 0, Qt::AlignLeft);
	auto rateKnob = modern::makeKnob(this, &c->m_lfoRateModel, tr("RATE"), tr("LFO rate:"), " Hz");
	auto lfoGroup = makeGroup(this, tr("LFO"), {shapeColumn, rateKnob,
		modern::makeKnob(this, &c->m_lfoVowelModel, tr("VOWEL"), tr("LFO to vowel:"), ""),
		modern::makeKnob(this, &c->m_lfoFormantModel, tr("FORMANT"), tr("LFO to formant:"), " st")});

	auto envGroup = makeGroup(this, tr("ENVELOPE"), {
		modern::makeKnob(this, &c->m_envVowelModel, tr("VOWEL"),
			tr("Input level to vowel (louder moves the vowel by this much):"), ""),
		modern::makeKnob(this, &c->m_envReleaseModel, tr("RELEASE"), tr("Envelope release:"), " ms")});

	auto modulation = new QHBoxLayout();
	modulation->setSpacing(14);
	modulation->addWidget(lfoGroup);
	modulation->addWidget(envGroup);
	modulation->addStretch(1);
	root->addLayout(modulation);

	// Grey out what the current engine doesn't use
	const auto updateEnabled = [=] {
		const bool spectral = c->mode() == FormantShifterControls::Mode::Spectral;
		for (QWidget* w : std::initializer_list<QWidget*>{pitchKnob, fineKnob, preserve, imprintKnob})
		{
			w->setEnabled(spectral);
		}
		rateKnob->setEnabled(c->lfoSyncTicks() <= 0.f);
	};
	connect(&c->m_modeModel, &Model::dataChanged, this, updateEnabled);
	connect(&c->m_lfoSyncModel, &Model::dataChanged, this, updateEnabled);
	updateEnabled();
}




FormantDisplay::FormantDisplay(FormantShifterControls* controls, QWidget* parent) :
	QWidget(parent),
	m_controls(controls),
	m_inputSpectrum(FormantShifterControls::DisplayPoints, MinDb),
	m_outputSpectrum(FormantShifterControls::DisplayPoints, MinDb)
{
	setMinimumSize(sizeHint());
	setCursor(Qt::SizeHorCursor);
	setMouseTracking(false);
	setToolTip(tr("Drag sideways to shift the formants (Shift: fine), scroll to morph the vowel, "
		"double-click to reset the shift"));

	m_fftIn = static_cast<float*>(fftwf_malloc(sizeof(float) * AnalysisSize));
	m_fftOut = fftwf_malloc(sizeof(fftwf_complex) * (AnalysisSize / 2 + 1));
	m_plan = fftwf_plan_dft_r2c_1d(AnalysisSize, m_fftIn, static_cast<fftwf_complex*>(m_fftOut), FFTW_ESTIMATE);

	for (Model* m : std::initializer_list<Model*>{&controls->m_modeModel, &controls->m_formantModel,
		&controls->m_voiceModel, &controls->m_vowelModel, &controls->m_resonanceModel, &controls->m_imprintModel})
	{
		connect(m, &Model::dataChanged, this, qOverload<>(&QWidget::update));
	}

	auto timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, [this] {
		if (!isVisible()) { return; }
		analyse();
		update();
	});
	timer->start(33);
}


FormantDisplay::~FormantDisplay()
{
	fftwf_destroy_plan(static_cast<fftwf_plan>(m_plan));
	fftwf_free(m_fftIn);
	fftwf_free(m_fftOut);
}


void FormantDisplay::analyse()
{
	const auto& scope = m_controls->m_scope;
	const float sr = Engine::audioEngine()->outputSampleRate();
	const auto out = static_cast<fftwf_complex*>(m_fftOut);
	const std::size_t start = scope.writePosition();
	for (int channel = 0; channel < 2; ++channel)
	{
		for (int i = 0; i < AnalysisSize; ++i)
		{
			const float w = 0.5f - 0.5f * std::cos(2.f * std::numbers::pi_v<float> * i / AnalysisSize);
			const auto at = start + static_cast<std::size_t>(i);
			m_fftIn[i] = (channel == 0 ? scope.left(at) : scope.right(at)) * w;
		}
		fftwf_execute(static_cast<fftwf_plan>(m_plan));
		auto& spectrum = channel == 0 ? m_inputSpectrum : m_outputSpectrum;
		const float binsPerHz = AnalysisSize / sr;
		const int lastBin = AnalysisSize / 2;
		for (int p = 0; p < FormantShifterControls::DisplayPoints; ++p)
		{
			// Strongest bin within this point's share of the axis; interpolate where bins are sparse
			const float lo = FormantShifterControls::displayFrequency(p) * binsPerHz;
			const float hi = FormantShifterControls::displayFrequency(p + 1) * binsPerHz;
			float mag = 0.f;
			if (hi - lo < 1.f)
			{
				const int i = std::clamp(static_cast<int>(lo), 0, lastBin - 1);
				const float t = std::clamp(lo - i, 0.f, 1.f);
				const float a = std::hypot(out[i][0], out[i][1]);
				const float b = std::hypot(out[i + 1][0], out[i + 1][1]);
				mag = a + (b - a) * t;
			}
			else
			{
				for (int i = static_cast<int>(lo); i <= std::min(static_cast<int>(hi), lastBin); ++i)
				{
					mag = std::max(mag, std::hypot(out[i][0], out[i][1]));
				}
			}
			const float db = 20.f * std::log10(std::max(mag * 4.f / AnalysisSize, 1e-6f));
			// Fast rise, slow fall, like an analyser
			spectrum[p] = db > spectrum[p] ? db : std::max(db, spectrum[p] - 2.f);
		}
	}
}


QRectF FormantDisplay::plotRect() const
{
	return QRectF(rect()).adjusted(1, 14, -1, -14);
}


float FormantDisplay::xForFrequency(const QRectF& plot, float freq) const
{
	return plot.left() + plot.width() * std::log(freq / MinFreq) / std::log(MaxFreq / MinFreq);
}


float FormantDisplay::yForDb(const QRectF& plot, float db) const
{
	return plot.bottom() - plot.height() * (std::clamp(db, MinDb, MaxDb) - MinDb) / (MaxDb - MinDb);
}


void FormantDisplay::paintEvent(QPaintEvent*)
{
	using namespace formant;
	QPainter p(this);
	const QRectF area = QRectF(rect());
	modern::paintDisplay(p, area, 0, 4);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF plot = plotRect();
	const auto c = m_controls;
	const bool spectral = c->mode() == FormantShifterControls::Mode::Spectral;

	QFont small = adjustedToPixelSize(font(), SMALL_FONT_SIZE);
	p.setFont(small);
	const auto fm = QFontMetrics(small);

	// Frequency grid
	for (float f : {50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f})
	{
		const float x = xForFrequency(plot, f);
		const bool major = f == 100.f || f == 1000.f || f == 10000.f;
		p.setPen(QPen(major ? modern::gridColor() : QColor(255, 255, 255, 10), 1));
		p.drawLine(QPointF(x, area.top() + 3), QPointF(x, area.bottom() - 3));
		p.setPen(modern::dimTextColor());
		const QString label = f >= 1000.f ? QString("%1k").arg(f / 1000.f) : QString::number(f);
		p.drawText(QPointF(x + 3, area.bottom() - 4), label);
	}

	const auto pathFor = [&](auto valueAt) {
		QPainterPath path;
		for (int i = 0; i < FormantShifterControls::DisplayPoints; ++i)
		{
			const QPointF pt(xForFrequency(plot, FormantShifterControls::displayFrequency(i)), yForDb(plot, valueAt(i)));
			if (i == 0) { path.moveTo(pt); } else { path.lineTo(pt); }
		}
		return path;
	};

	// Spectra: input as a dim fill, output as a thin line
	QPainterPath input = pathFor([&](int i) { return m_inputSpectrum[i]; });
	QPainterPath fill = input;
	fill.lineTo(plot.right(), plot.bottom());
	fill.lineTo(plot.left(), plot.bottom());
	fill.closeSubpath();
	p.fillPath(fill, QColor(255, 255, 255, 22));
	QColor outColor = modern::accentColor();
	outColor.setAlpha(90);
	p.setPen(QPen(outColor, 1));
	p.drawPath(pathFor([&](int i) { return m_outputSpectrum[i]; }));

	const float liveFormant = c->m_liveFormant.load(std::memory_order_relaxed);
	const float liveVowel = c->m_liveVowel.load(std::memory_order_relaxed);
	const int voice = c->m_voiceModel.value();
	const float ratio = std::exp2(liveFormant / 12.f);
	const VowelShape vowel = scaledVowel(vowelAt(voice, liveVowel), ratio, 1.f);

	if (spectral)
	{
		if (c->m_envelopeValid.load(std::memory_order_acquire))
		{
			QColor inColor = modern::textColor();
			inColor.setAlpha(110);
			p.setPen(QPen(inColor, 1, Qt::DashLine));
			p.drawPath(pathFor([&](int i) { return c->m_inputEnvelope[i].load(std::memory_order_relaxed); }));
			p.setPen(QPen(modern::accentColor(), 2));
			p.drawPath(pathFor([&](int i) { return c->m_outputEnvelope[i].load(std::memory_order_relaxed); }));
		}
	}
	else
	{
		// The filter's response, with 0 dB drawn at -18 dBFS so it sits over typical material
		const float resonance = c->m_resonanceModel.value();
		const float makeup = vowelFilterMakeup(vowel, resonance);
		p.setPen(QPen(modern::accentColor(), 2));
		p.drawPath(pathFor([&](int i) {
			const float f = FormantShifterControls::displayFrequency(i);
			return -18.f + 20.f * std::log10(std::max(std::abs(vowelFilterResponse(vowel, f, resonance)) * makeup, 1e-5f));
		}));
	}

	// Formant markers wherever the vowel matters
	const bool showVowel = !spectral || c->m_imprintModel.value() > 0.f;
	if (showVowel)
	{
		for (int i = 0; i < FormantCount; ++i)
		{
			const float x = xForFrequency(plot, vowel[i].freq);
			if (x > plot.right()) { continue; }
			QColor tick = modern::accentColor();
			tick.setAlpha(i < 2 ? 200 : 110);
			p.setPen(QPen(tick, 1));
			p.drawLine(QPointF(x, area.top() + 2), QPointF(x, area.top() + 8));
			if (i < 3)
			{
				p.setPen(modern::dimTextColor());
				p.drawText(QPointF(x + 2, area.top() + 2 + fm.capHeight()), QString("F%1").arg(i + 1));
			}
		}
	}

	// Readout
	QStringList parts;
	if (std::abs(liveFormant) >= 0.005f || !showVowel) { parts << tr("formant %1 st").arg(liveFormant, 0, 'f', 1); }
	if (spectral)
	{
		const float pitch = c->m_pitchModel.value() + c->m_fineModel.value() * 0.01f;
		if (std::abs(pitch) >= 0.005f)
		{
			parts << tr("pitch %1%2 st%3").arg(pitch > 0 ? "+" : "").arg(pitch, 0, 'f', 2)
				.arg(c->m_preserveModel.value() ? "" : tr(" (formants follow)"));
		}
	}
	if (showVowel)
	{
		const int a = std::min(static_cast<int>(liveVowel), VowelCount - 2);
		const float t = liveVowel - a;
		const QString vowelName = t < 0.02f ? VowelLetters[a] : t > 0.98f ? VowelLetters[a + 1]
			: QString("%1 > %2 %3%").arg(VowelLetters[a], VowelLetters[a + 1]).arg(static_cast<int>(t * 100));
		parts << tr("%1 %2").arg(c->m_voiceModel.currentText().toLower(), vowelName);
	}
	// Bottom right, above the frequency labels, on a backing so the curves don't hide it
	const QString text = parts.join("  ·  ");
	const QRectF box(area.right() - 12 - fm.horizontalAdvance(text), plot.bottom() - fm.height() - 4,
		fm.horizontalAdvance(text) + 8, fm.height() + 2);
	QColor backing = modern::displayColor();
	backing.setAlpha(200);
	p.fillRect(box, backing);
	p.setPen(modern::textColor());
	p.drawText(box, Qt::AlignCenter, text);
}


void FormantDisplay::mousePressEvent(QMouseEvent* me)
{
	m_dragStart = position(me);
	m_formantAtPress = m_controls->m_formantModel.value();
}


void FormantDisplay::mouseMoveEvent(QMouseEvent* me)
{
	if (!(me->buttons() & Qt::LeftButton)) { return; }
	// Grab the envelope and slide it: one pixel moves the formants by one pixel of the frequency axis
	const float semitonesPerPixel = 12.f * std::log2(MaxFreq / MinFreq) / static_cast<float>(plotRect().width());
	const float fine = me->modifiers() & Qt::ShiftModifier ? 0.1f : 1.f;
	const float dx = static_cast<float>(position(me).x() - m_dragStart.x());
	m_controls->m_formantModel.setValue(m_formantAtPress + dx * semitonesPerPixel * fine);
}


void FormantDisplay::mouseDoubleClickEvent(QMouseEvent*)
{
	m_controls->m_formantModel.setValue(0.f);
}


void FormantDisplay::wheelEvent(QWheelEvent* we)
{
	const float step = (we->modifiers() & Qt::ShiftModifier ? 0.02f : 0.1f) * (we->angleDelta().y() > 0 ? 1.f : -1.f);
	m_controls->m_vowelModel.setValue(m_controls->m_vowelModel.value() + step);
	we->accept();
}




VowelStrip::VowelStrip(FormantShifterControls* controls, QWidget* parent) :
	QWidget(parent),
	FloatModelView(&controls->m_vowelModel, this),
	m_controls(controls)
{
	setMinimumSize(sizeHint());
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setCursor(Qt::PointingHandCursor);
	setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE + 1));
	setToolTip(tr("Vowel: click or drag to morph, double-click a letter to snap to it, right-click to automate. "
		"In spectral mode, turn up IMPRINT to hear it."));
	doConnections();

	auto timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, [this] { if (isVisible()) { update(); } });
	timer->start(33);
}


float VowelStrip::xFor(float value) const
{
	const float cell = width() / static_cast<float>(formant::VowelCount);
	return cell * (value + 0.5f);
}


float VowelStrip::valueAt(float x) const
{
	const float cell = width() / static_cast<float>(formant::VowelCount);
	return std::clamp(x / cell - 0.5f, 0.f, static_cast<float>(formant::VowelCount - 1));
}


void VowelStrip::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	p.setPen(QPen(modern::sectionBorder(), 1));
	p.setBrush(modern::buttonColor());
	p.drawRoundedRect(r, 2, 2);

	const float value = model()->value();
	const float live = m_controls->m_liveVowel.load(std::memory_order_relaxed);
	// In spectral mode the vowel only does something once there is some imprint
	const bool audible = isEnabled() && (m_controls->mode() == FormantShifterControls::Mode::Resonant
		|| m_controls->m_imprintModel.value() > 0.f);
	const QColor accent = audible ? modern::accentColor() : modern::dimTextColor();

	// The path the morph takes, filled up to the set position
	const float y = r.bottom() - 4;
	p.setPen(QPen(modern::sectionBorder(), 2));
	p.drawLine(QPointF(xFor(0), y), QPointF(xFor(formant::VowelCount - 1), y));
	p.setPen(QPen(accent, 2));
	p.drawLine(QPointF(xFor(0), y), QPointF(xFor(value), y));

	const float cell = width() / static_cast<float>(formant::VowelCount);
	for (int i = 0; i < formant::VowelCount; ++i)
	{
		// Letters light up by how close the vowel is to them
		const float closeness = std::clamp(1.f - std::abs(value - i), 0.f, 1.f);
		QColor color = modern::dimTextColor();
		color.setRgbF(color.redF() + (accent.redF() - color.redF()) * closeness,
			color.greenF() + (accent.greenF() - color.greenF()) * closeness,
			color.blueF() + (accent.blueF() - color.blueF()) * closeness);
		p.setPen(color);
		p.drawText(QRectF(i * cell, 0, cell, height() - 5), Qt::AlignCenter, VowelLetters[i]);
	}

	// Ghost marker where the modulation currently has the vowel
	if (std::abs(live - value) > 0.01f)
	{
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, 150));
		p.drawEllipse(QPointF(xFor(live), y), 2.5, 2.5);
	}
	p.setBrush(accent);
	p.drawRect(QRectF(xFor(value) - 2, y - 3, 4, 6));
}


void VowelStrip::mousePressEvent(QMouseEvent* me)
{
	if (me->button() != Qt::LeftButton)
	{
		QWidget::mousePressEvent(me);
		return;
	}
	model()->setValue(valueAt(static_cast<float>(position(me).x())));
}


void VowelStrip::mouseMoveEvent(QMouseEvent* me)
{
	if (me->buttons() & Qt::LeftButton) { model()->setValue(valueAt(static_cast<float>(position(me).x()))); }
}


void VowelStrip::mouseDoubleClickEvent(QMouseEvent* me)
{
	model()->setValue(std::round(valueAt(static_cast<float>(position(me).x()))));
}


void VowelStrip::wheelEvent(QWheelEvent* we)
{
	const float step = (we->modifiers() & Qt::ShiftModifier ? 0.01f : 0.05f) * (we->angleDelta().y() > 0 ? 1.f : -1.f);
	model()->setValue(model()->value() + step);
	we->accept();
}


void VowelStrip::contextMenuEvent(QContextMenuEvent*)
{
	CaptionMenu contextMenu(model()->displayName());
	addDefaultActions(&contextMenu);
	contextMenu.exec(QCursor::pos());
}

} // namespace lmms::gui
