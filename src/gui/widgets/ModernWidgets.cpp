/*
 * ModernWidgets.cpp - vector-drawn building blocks for modernized plugin UIs
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

#include "ModernWidgets.h"

#include <QGridLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QToolTip>
#include <QHelpEvent>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>

#include "CaptionMenu.h"
#include "ComboBoxModel.h"
#include "FontHelper.h"
#include "Graph.h"
#include "Knob.h"
#include "lmms_math.h"

namespace lmms::gui
{

namespace modern
{

void applyWindowStyle(QWidget* widget)
{
	widget->setAutoFillBackground(true);
	QPalette pal = widget->palette();
	pal.setColor(QPalette::Window, windowColor());
	pal.setColor(QPalette::WindowText, textColor());
	widget->setPalette(pal);
}

void paintDisplay(QPainter& p, const QRectF& rect, int verticalLines, int horizontalLines)
{
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(QPen(QColor(0, 0, 0, 200), 1));
	p.setBrush(displayColor());
	p.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);

	p.setPen(QPen(gridColor(), 1));
	for (int i = 1; i < verticalLines; ++i)
	{
		const double x = rect.left() + rect.width() * i / verticalLines;
		p.drawLine(QPointF(x, rect.top() + 3), QPointF(x, rect.bottom() - 3));
	}
	for (int i = 1; i < horizontalLines; ++i)
	{
		const double y = rect.top() + rect.height() * i / horizontalLines;
		p.drawLine(QPointF(rect.left() + 3, y), QPointF(rect.right() - 3, y));
	}
	p.restore();
}

Knob* makeKnob(QWidget* parent, FloatModel* model, const QString& label,
	const QString& hint, const QString& unit)
{
	auto knob = new Knob(KnobType::Modern, label, parent);
	knob->setModel(model);
	knob->setHintText(hint, unit);
	return knob;
}

} // namespace modern




ModernSection::ModernSection(const QString& title, QWidget* parent) :
	QWidget(parent),
	m_title(title),
	m_grid(new QGridLayout(this))
{
	const int top = m_title.isEmpty() ? 8 : 20;
	m_grid->setContentsMargins(8, top, 8, 8);
	m_grid->setHorizontalSpacing(6);
	m_grid->setVerticalSpacing(6);
}

QFont ModernSection::titleFont() const
{
	auto f = adjustedToPixelSize(font(), SMALL_FONT_SIZE);
	f.setBold(true);
	f.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
	return f;
}

QSize ModernSection::minimumSizeHint() const
{
	const QSize base = QWidget::minimumSizeHint();
	if (m_title.isEmpty()) { return base; }
	const int titleWidth = QFontMetrics(titleFont()).horizontalAdvance(m_title.toUpper()) + 16;
	return QSize(std::max(base.width(), titleWidth), base.height());
}

QSize ModernSection::sizeHint() const
{
	return QWidget::sizeHint().expandedTo(minimumSizeHint());
}

void ModernSection::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(QPen(modern::sectionBorder(), 1));
	p.setBrush(modern::sectionColor());
	p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);

	if (m_title.isEmpty()) { return; }

	p.setFont(titleFont());
	p.setPen(modern::textColor());
	p.drawText(QRectF(0, 3, width(), 14), Qt::AlignHCenter | Qt::AlignVCenter, m_title.toUpper());
}




ModernToggle::ModernToggle(const QString& text, QWidget* parent) :
	AutomatableButton(parent, text)
{
	setText(text);
	setCheckable(true);
	setCursor(Qt::PointingHandCursor);
	setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

QSize ModernToggle::sizeHint() const
{
	const auto fm = QFontMetrics(font());
	return QSize(fm.horizontalAdvance(text()) + 16, 18);
}

void ModernToggle::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);

	// Styled after the Compressor's mode buttons: grey when off, green text on dark green when on
	const bool on = model() && model()->value();
	const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	p.setPen(QPen(modern::sectionBorder(), 1));
	p.setBrush(on ? modern::activeButtonColor() : modern::buttonColor());
	p.drawRoundedRect(r, 2, 2);

	QColor textColor = on ? modern::accentColor() : modern::textColor();
	if (!isEnabled()) { textColor = modern::dimTextColor(); }
	p.setPen(textColor);
	p.setFont(font());
	p.drawText(rect(), Qt::AlignCenter, text());
}




ModernSegmented::ModernSegmented(QWidget* parent) :
	QWidget(parent),
	IntModelView(new ComboBoxModel(nullptr, QString(), true), this)
{
	setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	setCursor(Qt::PointingHandCursor);
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	doConnections();
}

void ModernSegmented::setItemPainter(ItemPainter painter, int itemWidth)
{
	m_itemPainter = std::move(painter);
	m_itemWidth = itemWidth;
	updateGeometry();
	update();
}

void ModernSegmented::modelChanged()
{
	updateGeometry();
	update();
}

int ModernSegmented::count() const
{
	if (!m_labels.isEmpty()) { return m_labels.size(); }
	if (!model()) { return 0; }
	if (auto combo = dynamic_cast<const ComboBoxModel*>(model())) { return combo->size(); }
	return model()->maxValue() - model()->minValue() + 1;
}

QString ModernSegmented::label(int index) const
{
	if (!m_labels.isEmpty()) { return m_labels.value(index); }
	if (auto combo = dynamic_cast<const ComboBoxModel*>(model())) { return combo->itemText(index); }
	return model() ? QString::number(model()->minValue() + index) : QString();
}

QSize ModernSegmented::sizeHint() const
{
	const int n = count();
	if (m_itemPainter) { return QSize(std::max(m_itemWidth * n, 40), 18); }
	const auto fm = QFontMetrics(font());
	int w = 0;
	for (int i = 0; i < n; ++i)
	{
		w = std::max(w, fm.horizontalAdvance(label(i)) + 14);
	}
	return QSize(std::max(w * n, 40), 18);
}

int ModernSegmented::indexAt(int x) const
{
	const int n = count();
	if (n == 0) { return -1; }
	return std::clamp(x * n / std::max(1, width()), 0, n - 1);
}

bool ModernSegmented::event(QEvent* e)
{
	if (e->type() == QEvent::ToolTip && !m_tips.isEmpty())
	{
		auto help = static_cast<QHelpEvent*>(e);
		const int index = indexAt(help->pos().x());
		if (index >= 0 && index < m_tips.size()) { QToolTip::showText(help->globalPos(), m_tips[index], this); }
		return true;
	}
	return QWidget::event(e);
}

void ModernSegmented::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);

	const bool outline = m_style == Style::Outline;
	const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	p.setPen(QPen(outline ? QColor(150, 155, 160) : modern::sectionBorder(), 1));
	p.setBrush(outline ? QColor(0, 0, 0) : modern::buttonColor());
	p.drawRoundedRect(r, 2, 2);

	const int n = count();
	if (n == 0 || !model()) { return; }

	const double segment = r.width() / n;
	const int current = model()->value() - model()->minValue();
	p.setFont(font());
	for (int i = 0; i < n; ++i)
	{
		const QRectF seg(r.left() + i * segment, r.top(), segment, r.height());
		QColor fg = modern::textColor();
		if (i > 0)
		{
			p.setPen(QPen(outline ? QColor(90, 94, 98) : modern::sectionBorder(), 1));
			p.drawLine(QPointF(seg.left(), seg.top() + 1), QPointF(seg.left(), seg.bottom() - 1));
		}
		if (i == current)
		{
			if (!outline)
			{
				p.setPen(Qt::NoPen);
				p.setBrush(modern::activeButtonColor());
				p.drawRect(seg.adjusted(1, 1, -1, -1));
			}
			fg = isEnabled() ? modern::accentColor() : modern::dimTextColor();
		}
		if (m_itemPainter)
		{
			p.save();
			m_itemPainter(p, seg.adjusted(4, 4, -4, -4), i, fg);
			p.restore();
		}
		else
		{
			p.setPen(fg);
			p.drawText(seg, Qt::AlignCenter, label(i));
		}
	}
}

void ModernSegmented::mousePressEvent(QMouseEvent* event)
{
	if (!model() || event->button() != Qt::LeftButton)
	{
		QWidget::mousePressEvent(event);
		return;
	}
	const int index = indexAt(event->pos().x());
	if (index >= 0) { model()->setValue(model()->minValue() + index); }
	event->accept();
}

void ModernSegmented::mouseDoubleClickEvent(QMouseEvent* event)
{
	const int index = indexAt(event->pos().x());
	if (index >= 0) { emit itemDoubleClicked(index); }
}

void ModernSegmented::wheelEvent(QWheelEvent* event)
{
	if (!model()) { return; }
	const int direction = (event->angleDelta().y() < 0 ? 1 : -1) * (event->inverted() ? -1 : 1);
	model()->setValue(model()->value() + direction);
	event->accept();
}

void ModernSegmented::contextMenuEvent(QContextMenuEvent*)
{
	if (!model()) { return; }
	CaptionMenu contextMenu(model()->displayName());
	addDefaultActions(&contextMenu);
	contextMenu.exec(QCursor::pos());
}




ModernMeter::ModernMeter(QWidget* parent, const float* peakLeft, const float* peakRight) :
	QWidget(parent),
	m_peakLeft(peakLeft),
	m_peakRight(peakRight)
{
	setMinimumSize(12, 60);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
	// Own timer instead of MainWindow::periodicUpdate so the meter also works in detached/headless views
	auto timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &ModernMeter::refresh);
	timer->start(33);
}

void ModernMeter::refresh()
{
	auto follow = [](float peak, float& display, float& hold, int& holdCounter)
	{
		// Instant attack, smooth release so the meter is readable
		display = std::max(peak, display * 0.86f);
		if (peak >= hold) { hold = peak; holdCounter = 30; }
		else if (--holdCounter <= 0) { hold *= 0.92f; }
	};
	follow(m_peakLeft ? *m_peakLeft : 0.f, m_displayLeft, m_holdLeft, m_holdCounterLeft);
	follow(m_peakRight ? *m_peakRight : 0.f, m_displayRight, m_holdRight, m_holdCounterRight);
	update();
}

void ModernMeter::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(modern::displayColor());
	p.drawRoundedRect(rect(), 3, 3);

	// Map -60..+6 dBFS onto the height of the meter
	auto toY = [this](float amp)
	{
		const float db = amp > 0.f ? ampToDbfs(amp) : -60.f;
		const float norm = std::clamp((db + 60.f) / 66.f, 0.f, 1.f);
		return (height() - 2) * (1.f - norm) + 1.f;
	};

	QLinearGradient grad(0, height(), 0, 0);
	// Same colors as the mixer faders (peakOk / peakWarn / peakClip in style.css)
	grad.setColorAt(0.0, QColor(10, 212, 92));
	grad.setColorAt(0.8, QColor(10, 212, 92));
	grad.setColorAt(0.88, QColor(214, 236, 82));
	grad.setColorAt(0.93, QColor(193, 32, 56));

	const double barWidth = (width() - 3) / 2.0;
	const std::array<float, 2> levels = {m_displayLeft, m_displayRight};
	const std::array<float, 2> holds = {m_holdLeft, m_holdRight};
	for (int ch = 0; ch < 2; ++ch)
	{
		const double x = 1 + ch * (barWidth + 1);
		const float y = toY(levels[ch]);
		p.setBrush(grad);
		p.drawRect(QRectF(x, y, barWidth, height() - 1 - y));
		p.setBrush(holds[ch] > 1.f ? QColor(193, 32, 56) : modern::textColor());
		p.drawRect(QRectF(x, toY(holds[ch]), barWidth, 1.5));
	}

	// 0 dBFS tick
	p.setPen(QPen(QColor(255, 255, 255, 70), 1));
	const float zero = toY(1.f);
	p.drawLine(QPointF(0, zero), QPointF(width(), zero));
}





ModernStereoScope::ModernStereoScope(QWidget* parent, const ScopeBuffer* buffer) :
	QWidget(parent),
	m_buffer(buffer)
{
	setFixedSize(sizeHint());
	auto timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
	timer->start(33);
}

void ModernStereoScope::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	const QRectF area = QRectF(rect());
	modern::paintDisplay(p, area, 2, 2);
	p.setRenderHint(QPainter::Antialiasing);

	const QPointF c = area.center();
	const double radius = area.width() / 2.0 - 8.0;

	// Diagonal L / R guides
	p.setPen(QPen(modern::gridColor(), 1, Qt::DashLine));
	p.drawLine(QPointF(c.x() - radius * 0.7, c.y() - radius * 0.7), QPointF(c.x() + radius * 0.7, c.y() + radius * 0.7));
	p.drawLine(QPointF(c.x() + radius * 0.7, c.y() - radius * 0.7), QPointF(c.x() - radius * 0.7, c.y() + radius * 0.7));

	const std::size_t end = m_buffer->writePosition();
	double sumLR = 0, sumLL = 0, sumRR = 0;
	QColor dot = modern::accentColor();
	dot.setAlpha(120);
	p.setPen(QPen(dot, 1.5));
	for (std::size_t i = 0; i < ScopeBuffer::Size; ++i)
	{
		const float l = m_buffer->left(end + i);
		const float r = m_buffer->right(end + i);
		const float side = (l - r) * 0.7071f;
		const float mid = (l + r) * 0.7071f;
		// Soft-limit so loud material stays inside the display
		p.drawPoint(QPointF(c.x() + radius * std::tanh(side), c.y() - radius * std::tanh(mid)));
		sumLR += l * r;
		sumLL += l * l;
		sumRR += r * r;
	}
	const double norm = std::sqrt(sumLL * sumRR);
	const float target = norm > 1e-9 ? static_cast<float>(sumLR / norm) : 0.f;
	m_correlation += (target - m_correlation) * 0.2f;

	p.setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	p.setPen(modern::dimTextColor());
	p.drawText(QRectF(6, 4, 20, 12), Qt::AlignLeft, "L");
	p.drawText(QRectF(area.width() - 26, 4, 20, 12), Qt::AlignRight, "R");

	// Correlation bar: -1 (out of phase) .. +1 (mono)
	const QRectF bar(10, area.bottom() - 12, area.width() - 20, 4);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 255, 255, 30));
	p.drawRoundedRect(bar, 2, 2);
	const double pos = bar.left() + bar.width() * (m_correlation + 1.f) / 2.f;
	p.setBrush(m_correlation < 0 ? QColor(193, 32, 56) : modern::accentColor());
	p.drawEllipse(QPointF(pos, bar.center().y()), 3.5, 3.5);
}


GraphLevelIndicator::GraphLevelIndicator(QWidget* parent, const graphModel* curve,
	const std::atomic<float>* level, const QColor& color) :
	QWidget(parent),
	m_curve(curve),
	m_level(level),
	m_color(color)
{
	setAttribute(Qt::WA_TransparentForMouseEvents);
	auto timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, [this]
	{
		// Fast attack, slower release so the marker is easy to follow
		const float level = std::clamp(m_level->load(std::memory_order_relaxed), 0.f, 1.f);
		m_display = level > m_display ? level : m_display * 0.85f + level * 0.15f;
		update();
	});
	timer->start(33);
}

void GraphLevelIndicator::paintEvent(QPaintEvent*)
{
	if (m_display < 0.002f || m_curve->length() == 0) { return; }

	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);

	const int length = m_curve->length();
	const float position = m_display * (length - 1);
	const int index = static_cast<int>(position);
	const float frac = position - index;
	const float* samples = m_curve->samples();
	const float value = samples[index] + (samples[std::min(index + 1, length - 1)] - samples[index]) * frac;
	const float norm = (value - m_curve->minValue()) / (m_curve->maxValue() - m_curve->minValue());

	const double x = m_display * (width() - 1);
	const double y = (1.0 - std::clamp(norm, 0.f, 1.f)) * (height() - 1);

	QColor line = m_color;
	line.setAlpha(90);
	p.setPen(QPen(line, 1, Qt::DashLine));
	p.drawLine(QPointF(x, height()), QPointF(x, y));

	QRadialGradient glow(QPointF(x, y), 7);
	QColor halo = m_color;
	halo.setAlpha(150);
	glow.setColorAt(0, halo);
	halo.setAlpha(0);
	glow.setColorAt(1, halo);
	p.setPen(Qt::NoPen);
	p.setBrush(glow);
	p.drawEllipse(QPointF(x, y), 7, 7);
	p.setBrush(m_color.lighter(140));
	p.drawEllipse(QPointF(x, y), 2.5, 2.5);
}


} // namespace lmms::gui
