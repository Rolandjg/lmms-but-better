/*
 * FunctionGeneratorDialog.cpp - dialog for generating periodic shapes into an automation clip
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

#include "FunctionGeneratorDialog.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include <functional>
#include <random>

namespace lmms::gui
{

namespace
{

// settings are remembered between invocations (the value range is not, it depends on the clip)
FunctionGenerator s_lastSettings;

QPolygonF toPolyline(const std::vector<FunctionGenerator::Node>& nodes, const QRectF& area, double lengthTicks,
	float rangeMin, float rangeMax)
{
	const auto mapX = [&](double tick) { return area.left() + area.width() * tick / lengthTicks; };
	const auto mapY = [&](float v) {
		const float t = rangeMax > rangeMin ? (v - rangeMin) / (rangeMax - rangeMin) : 0.f;
		return area.bottom() - area.height() * std::clamp(t, 0.f, 1.f);
	};

	QPolygonF poly;
	for (const auto& n : nodes)
	{
		poly << QPointF(mapX(n.tick), mapY(n.in));
		if (n.out != n.in) { poly << QPointF(mapX(n.tick), mapY(n.out)); }
	}
	return poly;
}

} // namespace


//! Draws the generated curve over the clip's value range
class FunctionPreview : public QWidget
{
	Q_OBJECT
	Q_PROPERTY(QColor backgroundColor MEMBER m_background)
	Q_PROPERTY(QColor gridColor MEMBER m_grid)
	Q_PROPERTY(QColor lineColor MEMBER m_line)
	Q_PROPERTY(QColor textColor MEMBER m_text)
public:
	FunctionPreview(QWidget* parent)
		: QWidget(parent)
		, m_background(0x12, 0x14, 0x18)
		, m_grid(0x2a, 0x2f, 0x37)
		, m_line(0x1d, 0xe2, 0x76)
		, m_text(0x8d, 0x96, 0xa3)
	{
		setMinimumSize(360, 150);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	}

	void setData(std::vector<FunctionGenerator::Node> nodes, double cycles, double phase, int lengthTicks,
		float rangeMin, float rangeMax)
	{
		m_nodes = std::move(nodes);
		m_cycles = cycles;
		m_phase = phase;
		m_length = lengthTicks;
		m_min = rangeMin;
		m_max = rangeMax;
		update();
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		p.fillRect(rect(), m_background);

		const QRectF area = QRectF(rect()).adjusted(0.5, 8.5, -0.5, -8.5);

		// quarter lines for the value, one line per cycle boundary
		p.setPen(QPen(m_grid, 1));
		for (int i = 0; i <= 4; ++i)
		{
			const double y = std::round(area.top() + area.height() * i / 4.0) + 0.5;
			p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
		}
		if (m_cycles > 0 && m_cycles <= 64)
		{
			for (double c = std::ceil(m_phase) - m_phase; c < m_cycles; c += 1.0)
			{
				const double x = std::round(area.left() + area.width() * c / m_cycles) + 0.5;
				p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
			}
		}

		if (m_nodes.empty() || m_length <= 0) { return; }

		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(QPen(m_line, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.drawPolyline(toPolyline(m_nodes, area, m_length, m_min, m_max));

		p.setRenderHint(QPainter::Antialiasing, false);
		p.setPen(m_text);
		QFont f = font();
		f.setPixelSize(10);
		p.setFont(f);
		p.drawText(QRectF(area.left() + 4, area.top() + 2, 80, 12), Qt::AlignLeft | Qt::AlignTop,
			QString::number(m_max, 'g', 4));
		p.drawText(QRectF(area.left() + 4, area.bottom() - 14, 80, 12), Qt::AlignLeft | Qt::AlignBottom,
			QString::number(m_min, 'g', 4));
	}

private:
	std::vector<FunctionGenerator::Node> m_nodes;
	double m_cycles = 1.0;
	double m_phase = 0.0;
	int m_length = 0;
	float m_min = 0.f;
	float m_max = 1.f;
	QColor m_background;
	QColor m_grid;
	QColor m_line;
	QColor m_text;
};


//! Graph of a FunctionGenerator::Curve, edited by dragging points: click empty space to add a
//! point, drag to move it, right-click (or double-click) a point to remove it.
class CurveEditor : public QWidget
{
	Q_OBJECT
public:
	using Formatter = std::function<QString(double)>;

	CurveEditor(double offValue, Formatter format, QWidget* parent)
		: QWidget(parent)
		, m_off(offValue)
		, m_format(std::move(format))
	{
		setMinimumSize(150, 84);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		setMouseTracking(true);
		setCursor(Qt::CrossCursor);
	}

	const FunctionGenerator::Curve& curve() const { return m_curve; }
	void setCurve(const FunctionGenerator::Curve& curve)
	{
		if (m_dragging >= 0) { return; }
		m_curve = curve;
		m_hover = -1;
		update();
	}

	//! A few common shapes to start from; y values are 0..1
	void preset(const std::vector<std::pair<double, double>>& points)
	{
		m_curve.points = points;
		m_hover = -1;
		m_dragging = -1;
		update();
		emit edited();
	}

signals:
	void edited();

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		p.fillRect(rect(), QColor(0x12, 0x14, 0x18));

		const QRectF area = plot();
		p.setPen(QColor(0x2a, 0x2f, 0x37));
		for (int i = 0; i <= 4; ++i)
		{
			const double x = std::round(area.left() + area.width() * i / 4.0) + 0.5;
			const double y = std::round(area.top() + area.height() * i / 4.0) + 0.5;
			p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
			p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
		}

		const bool off = m_curve.empty();
		const QColor line = off ? QColor(0x5b, 0x63, 0x70) : QColor(0x1d, 0xe2, 0x76);

		QPolygonF poly;
		if (off) { poly << toWidget({0.0, m_off}) << toWidget({1.0, m_off}); }
		else
		{
			poly << toWidget({0.0, m_curve.points.front().second});
			for (const auto& pt : m_curve.points) { poly << toWidget(pt); }
			poly << toWidget({1.0, m_curve.points.back().second});
		}
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(QPen(line, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.drawPolyline(poly);
		p.setRenderHint(QPainter::Antialiasing, false);

		for (int i = 0; i < static_cast<int>(m_curve.points.size()); ++i)
		{
			const QPointF c = toWidget(m_curve.points[i]);
			const bool hot = i == m_hover || i == m_dragging;
			const QRectF box(std::round(c.x()) - 3.5, std::round(c.y()) - 3.5, 7, 7);
			p.fillRect(box, hot ? QColor(0xff, 0xff, 0xff) : QColor(0x1d, 0xe2, 0x76));
			p.setPen(QColor(0x0a, 0x0b, 0x0d));
			p.drawRect(box.adjusted(0, 0, -1, -1));
		}

		// readout of the point under the mouse
		const int shown = m_dragging >= 0 ? m_dragging : m_hover;
		if (shown >= 0 && shown < static_cast<int>(m_curve.points.size()))
		{
			const auto& pt = m_curve.points[shown];
			QFont f = font();
			f.setPixelSize(10);
			p.setFont(f);
			p.setPen(QColor(0xc8, 0xce, 0xd6));
			const QString text = m_format(pt.second) + QString("  @ %1%").arg(static_cast<int>(std::round(pt.first * 100)));
			const bool left = pt.first > 0.5;
			p.drawText(QRectF(area.left() + 4, area.top() + 2, area.width() - 8, 14),
				(left ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignTop, text);
		}
		else if (off)
		{
			QFont f = font();
			f.setPixelSize(10);
			p.setFont(f);
			p.setPen(QColor(0x6b, 0x73, 0x80));
			p.drawText(area.adjusted(6, 0, -6, -4), Qt::AlignHCenter | Qt::AlignBottom, tr("click to draw"));
		}
	}

	void mousePressEvent(QMouseEvent* me) override
	{
		const int hit = pointAt(me->pos());
		if (me->button() == Qt::RightButton)
		{
			if (hit >= 0) { removePoint(hit); }
			return;
		}
		if (me->button() != Qt::LeftButton) { return; }

		if (hit >= 0) { m_dragging = hit; }
		else
		{
			if (m_curve.empty()) { m_curve.points = {{0.0, m_off}, {1.0, m_off}}; }
			const auto pt = fromWidget(me->pos());
			const auto index = std::upper_bound(m_curve.points.begin(), m_curve.points.end(), pt.first,
				[](double v, const auto& q) { return v < q.first; }) - m_curve.points.begin();
			m_curve.points.insert(m_curve.points.begin() + index, pt);
			m_dragging = static_cast<int>(index);
		}
		drag(me->pos());
	}

	void mouseMoveEvent(QMouseEvent* me) override
	{
		if (m_dragging >= 0) { drag(me->pos()); }
		else
		{
			const int hit = pointAt(me->pos());
			if (hit != m_hover)
			{
				m_hover = hit;
				update();
			}
		}
	}

	void mouseReleaseEvent(QMouseEvent*) override
	{
		m_dragging = -1;
		update();
	}

	void mouseDoubleClickEvent(QMouseEvent* me) override
	{
		const int hit = pointAt(me->pos());
		if (hit >= 0) { removePoint(hit); }
	}

	void leaveEvent(QEvent*) override
	{
		m_hover = -1;
		update();
	}

private:
	QRectF plot() const { return QRectF(rect()).adjusted(6.5, 6.5, -6.5, -6.5); }

	QPointF toWidget(std::pair<double, double> pt) const
	{
		const QRectF a = plot();
		return {a.left() + a.width() * pt.first, a.bottom() - a.height() * pt.second};
	}

	std::pair<double, double> fromWidget(const QPointF& pos) const
	{
		const QRectF a = plot();
		return {std::clamp((pos.x() - a.left()) / a.width(), 0.0, 1.0),
			std::clamp((a.bottom() - pos.y()) / a.height(), 0.0, 1.0)};
	}

	int pointAt(const QPoint& pos) const
	{
		int best = -1;
		double bestDist = 8.0 * 8.0;
		for (int i = 0; i < static_cast<int>(m_curve.points.size()); ++i)
		{
			const QPointF d = toWidget(m_curve.points[i]) - pos;
			const double dist = d.x() * d.x() + d.y() * d.y();
			if (dist <= bestDist)
			{
				bestDist = dist;
				best = i;
			}
		}
		return best;
	}

	void drag(const QPoint& pos)
	{
		auto pt = fromWidget(pos);
		auto& points = m_curve.points;
		const int last = static_cast<int>(points.size()) - 1;

		// the ends keep their place in time; points in between stay between their neighbours
		if (m_dragging == 0 || m_dragging == last) { pt.first = points[m_dragging].first; }
		else { pt.first = std::clamp(pt.first, points[m_dragging - 1].first, points[m_dragging + 1].first); }
		points[m_dragging] = pt;
		update();
		emit edited();
	}

	void removePoint(int index)
	{
		m_curve.points.erase(m_curve.points.begin() + index);
		m_hover = -1;
		update();
		emit edited();
	}

	FunctionGenerator::Curve m_curve;
	double m_off;
	Formatter m_format;
	int m_hover = -1;
	int m_dragging = -1;
};


//! Flat checkable button showing a miniature of its shape
class ShapeButton : public QAbstractButton
{
	Q_OBJECT
public:
	ShapeButton(FunctionGenerator::Shape shape, const QString& tip, QWidget* parent)
		: QAbstractButton(parent)
		, m_shape(shape)
	{
		setCheckable(true);
		setToolTip(tip);
		setFixedHeight(34);
		setMinimumWidth(44);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		setCursor(Qt::PointingHandCursor);
	}

	FunctionGenerator::Shape shape() const { return m_shape; }

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		const QColor bg = isChecked() ? QColor(0x17, 0x3d, 0x2b) : underMouse() ? QColor(0x30, 0x35, 0x3d) : QColor(0x26, 0x2a, 0x30);
		const QColor edge = isChecked() ? QColor(0x1a, 0x7a, 0x47) : QColor(0x0a, 0x0b, 0x0d);
		p.fillRect(rect(), bg);
		p.setPen(edge);
		p.drawRect(rect().adjusted(0, 0, -1, -1));

		FunctionGenerator g;
		g.shape = m_shape;
		g.cycles = 2.0;
		g.cells = 5;
		g.seed = 7;
		g.detail = 24;
		const QRectF area = QRectF(rect()).adjusted(9, 8, -9, -8);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(QPen(isChecked() ? QColor(0x1d, 0xe2, 0x76) : QColor(0x9a, 0xa3, 0xb0), 1.5, Qt::SolidLine,
			Qt::RoundCap, Qt::RoundJoin));
		p.drawPolyline(toPolyline(g.build(100.0), area, 100.0, 0.f, 1.f));
	}

	void enterEvent(QEvent*) override { update(); }
	void leaveEvent(QEvent*) override { update(); }

private:
	FunctionGenerator::Shape m_shape;
};


//! Label + slider + number box row
class GeneratorParam : public QObject
{
	Q_OBJECT
public:
	GeneratorParam(const QString& name, double min, double max, double step, int decimals, const QString& suffix,
		QWidget* parent, QGridLayout* grid, int row, int column)
		: QObject(parent)
		, m_min(min)
		, m_max(max)
	{
		m_label = new QLabel(name, parent);
		m_slider = new QSlider(Qt::Horizontal, parent);
		m_slider->setRange(0, Resolution);
		m_slider->setMinimumWidth(110);
		m_spin = new QDoubleSpinBox(parent);
		m_spin->setRange(min, max);
		m_spin->setSingleStep(step);
		m_spin->setDecimals(decimals);
		m_spin->setSuffix(suffix);
		m_spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
		m_spin->setAlignment(Qt::AlignRight);
		m_spin->setFixedWidth(74);

		grid->addWidget(m_label, row, column * 3);
		grid->addWidget(m_slider, row, column * 3 + 1);
		grid->addWidget(m_spin, row, column * 3 + 2);

		connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
			m_spin->setValue(m_min + (m_max - m_min) * v / Resolution);
		});
		connect(m_spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v) {
			const QSignalBlocker block(m_slider);
			m_slider->setValue(static_cast<int>(std::round((std::clamp(v, m_min, m_max) - m_min) / (m_max - m_min) * Resolution)));
			emit changed();
		});
	}

	double value() const { return m_spin->value(); }
	void setValue(double v)
	{
		const QSignalBlocker block(m_spin);
		m_spin->setValue(v);
		const QSignalBlocker blockSlider(m_slider);
		m_slider->setValue(static_cast<int>(std::round((std::clamp(v, m_min, m_max) - m_min) / (m_max - m_min) * Resolution)));
	}
	void setLabel(const QString& text) { m_label->setText(text); }
	void setToolTip(const QString& tip)
	{
		m_label->setToolTip(tip);
		m_slider->setToolTip(tip);
		m_spin->setToolTip(tip);
	}
	void setEnabled(bool on)
	{
		m_label->setEnabled(on);
		m_slider->setEnabled(on);
		m_spin->setEnabled(on);
	}

signals:
	void changed();

private:
	static constexpr int Resolution = 1000;
	QLabel* m_label;
	QSlider* m_slider;
	QDoubleSpinBox* m_spin;
	double m_min;
	double m_max;
};


FunctionGeneratorDialog::FunctionGeneratorDialog(int lengthTicks, float rangeMin, float rangeMax, QWidget* parent)
	: QDialog(parent)
	, m_gen(s_lastSettings)
	, m_lengthTicks(lengthTicks)
	, m_rangeMin(rangeMin)
	, m_rangeMax(rangeMax)
{
	setWindowTitle(tr("Function generator"));

	m_gen.low = rangeMin;
	m_gen.high = rangeMax;

	auto root = new QVBoxLayout(this);
	root->setSpacing(10);

	// shape picker
	auto shapes = new QHBoxLayout;
	shapes->setSpacing(4);
	const struct { FunctionGenerator::Shape shape; const char* name; } kinds[] = {
		{FunctionGenerator::Shape::Sine, QT_TR_NOOP("Sine")},
		{FunctionGenerator::Shape::Triangle, QT_TR_NOOP("Triangle")},
		{FunctionGenerator::Shape::SawUp, QT_TR_NOOP("Saw up")},
		{FunctionGenerator::Shape::SawDown, QT_TR_NOOP("Saw down")},
		{FunctionGenerator::Shape::Square, QT_TR_NOOP("Square / pulse")},
		{FunctionGenerator::Shape::Noise, QT_TR_NOOP("Random steps")},
		{FunctionGenerator::Shape::SmoothNoise, QT_TR_NOOP("Smooth random")},
	};
	for (const auto& kind : kinds)
	{
		auto button = new ShapeButton(kind.shape, tr(kind.name), this);
		connect(button, &QAbstractButton::clicked, this, [this, shape = kind.shape] { setShape(shape); });
		shapes->addWidget(button);
		m_shapeButtons.push_back(button);
	}
	root->addLayout(shapes);

	m_preview = new FunctionPreview(this);
	root->addWidget(m_preview, 1);

	// shaping graphs: draw how amplitude and speed change from start to end
	auto graphs = new QHBoxLayout;
	graphs->setSpacing(10);
	const struct
	{
		const char* title;
		const char* tip;
		double off;
		CurveEditor** target;
		bool amplitude;
	} graphDefs[] = {
		{QT_TR_NOOP("Amplitude over time"),
			QT_TR_NOOP("Scales how far the wave swings. Draw a ramp for a fade in or out."), 1.0, &m_amplitude, true},
		{QT_TR_NOOP("Frequency over time"),
			QT_TR_NOOP("Speeds the wave up or down along the clip. The total number of cycles stays the same."), 0.5,
			&m_frequency, false},
	};
	for (const auto& def : graphDefs)
	{
		auto column = new QVBoxLayout;
		column->setSpacing(4);
		auto header = new QHBoxLayout;
		header->setSpacing(3);
		auto title = new QLabel(tr(def.title), this);
		title->setToolTip(tr(def.tip));
		header->addWidget(title, 1);

		const bool amp = def.amplitude;
		auto editor = new CurveEditor(def.off,
			[amp](double v) {
				return amp ? QString("%1%").arg(static_cast<int>(std::round(v * 100)))
						   : QString::fromUtf8("\u00d7%1").arg(std::pow(4.0, 2.0 * v - 1.0), 0, 'g', 3);
			},
			this);
		editor->setToolTip(tr(def.tip) + "\n" + tr("Click to add a point, drag to move it, right-click to remove it."));
		*def.target = editor;

		const struct { const char* label; const char* tip; std::vector<std::pair<double, double>> points; } presets[] = {
			{QT_TR_NOOP("Up"), QT_TR_NOOP("Rise"), {{0.0, 0.0}, {1.0, 1.0}}},
			{QT_TR_NOOP("Down"), QT_TR_NOOP("Fall"), {{0.0, 1.0}, {1.0, 0.0}}},
			{QT_TR_NOOP("Hill"), QT_TR_NOOP("Up, then down"), {{0.0, 0.0}, {0.5, 1.0}, {1.0, 0.0}}},
			{QT_TR_NOOP("Dip"), QT_TR_NOOP("Down, then up"), {{0.0, 1.0}, {0.5, 0.0}, {1.0, 1.0}}},
			{QT_TR_NOOP("Off"), QT_TR_NOOP("Remove the graph"), {}},
		};
		for (const auto& preset : presets)
		{
			auto b = new QToolButton(this);
			b->setText(tr(preset.label));
			b->setToolTip(tr(preset.tip));
			b->setAutoRaise(true);
			connect(b, &QToolButton::clicked, editor, [editor, points = preset.points] { editor->preset(points); });
			header->addWidget(b);
		}
		column->addLayout(header);
		column->addWidget(editor, 1);
		graphs->addLayout(column, 1);
		connect(editor, &CurveEditor::edited, this, &FunctionGeneratorDialog::paramsChanged);
	}
	root->addLayout(graphs);

	// parameters: two columns of label / slider / value
	auto grid = new QGridLayout;
	grid->setHorizontalSpacing(8);
	grid->setVerticalSpacing(6);
	grid->setColumnMinimumWidth(3, 14);
	grid->setColumnStretch(1, 1);
	grid->setColumnStretch(4, 1);

	const double span = rangeMax - rangeMin;
	const double step = span > 0 ? span / 100.0 : 0.01;
	m_cycles = new GeneratorParam(tr("Cycles"), 0.25, 32.0, 0.25, 2, "", this, grid, 0, 0);
	m_phase = new GeneratorParam(tr("Phase"), 0.0, 360.0, 5.0, 0, tr("°"), this, grid, 1, 0);
	m_symmetry = new GeneratorParam(tr("Symmetry"), 1.0, 99.0, 1.0, 0, "%", this, grid, 2, 0);
	m_bend = new GeneratorParam(tr("Curve"), -100.0, 100.0, 5.0, 0, "%", this, grid, 3, 0);
	m_steps = new GeneratorParam(tr("Quantize"), 0.0, 32.0, 1.0, 0, "", this, grid, 0, 1);
	m_cells = new GeneratorParam(tr("Values / cycle"), 1.0, 64.0, 1.0, 0, "", this, grid, 1, 1);
	m_detail = new GeneratorParam(tr("Detail"), 4.0, 128.0, 1.0, 0, "", this, grid, 2, 1);
	m_low = new GeneratorParam(tr("Low"), rangeMin, rangeMax, step, 3, "", this, grid, 3, 1);
	m_high = new GeneratorParam(tr("High"), rangeMin, rangeMax, step, 3, "", this, grid, 4, 1);
	m_freqRange = new GeneratorParam(tr("Speed range"), 0.5, 4.0, 0.25, 2, tr(" oct"), this, grid, 5, 0);
	m_freqRange->setToolTip(tr("How many octaves of speed change the frequency graph spans in each direction"));
	root->addLayout(grid);

	for (auto param : {m_cycles, m_phase, m_symmetry, m_bend, m_steps, m_cells, m_detail, m_low, m_high, m_freqRange})
	{
		connect(param, &GeneratorParam::changed, this, &FunctionGeneratorDialog::paramsChanged);
	}

	m_invert = new QCheckBox(tr("Invert"), this);
	connect(m_invert, &QCheckBox::toggled, this, &FunctionGeneratorDialog::paramsChanged);
	auto reseed = new QPushButton(tr("New random seed"), this);
	connect(reseed, &QPushButton::clicked, this, &FunctionGeneratorDialog::reseed);
	grid->addWidget(m_invert, 4, 0, 1, 3);
	grid->addWidget(reseed, 5, 4, 1, 2, Qt::AlignRight);
	m_reseedButton = reseed;

	// advanced: a formula instead of one of the stock shapes
	m_expressionToggle = new QToolButton(this);
	m_expressionToggle->setText(tr("Math expression"));
	m_expressionToggle->setToolTip(tr("Define the wave with a formula instead of a stock shape"));
	m_expressionToggle->setCheckable(true);
	m_expressionToggle->setAutoRaise(true);
	m_expressionToggle->setArrowType(Qt::RightArrow);
	m_expressionToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	connect(m_expressionToggle, &QToolButton::toggled, this, &FunctionGeneratorDialog::toggleExpression);
	root->addWidget(m_expressionToggle, 0, Qt::AlignLeft);

	m_expressionBox = new QWidget(this);
	auto exprLayout = new QVBoxLayout(m_expressionBox);
	exprLayout->setContentsMargins(0, 0, 0, 0);
	exprLayout->setSpacing(4);
	m_expression = new QLineEdit(m_expressionBox);
	m_expression->setPlaceholderText(tr("sin(tau * p) * (1 - t)"));
	connect(m_expression, &QLineEdit::textChanged, this, &FunctionGeneratorDialog::expressionChanged);
	m_expressionStatus = new QLabel(m_expressionBox);
	m_expressionStatus->setWordWrap(true);
	exprLayout->addWidget(m_expression);
	exprLayout->addWidget(m_expressionStatus);
	m_expressionBox->hide();
	root->addWidget(m_expressionBox);

	// footer
	m_info = new QLabel(this);
	m_info->setEnabled(false);
	auto buttons = new QDialogButtonBox(this);
	auto reset = buttons->addButton(tr("Reset"), QDialogButtonBox::ResetRole);
	auto apply = buttons->addButton(tr("Generate"), QDialogButtonBox::AcceptRole);
	buttons->addButton(QDialogButtonBox::Cancel);
	apply->setDefault(true);
	connect(reset, &QPushButton::clicked, this, &FunctionGeneratorDialog::resetParams);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(this, &QDialog::accepted, this, [this] {
		s_lastSettings = m_gen;
	});

	auto footer = new QHBoxLayout;
	footer->addWidget(m_info, 1);
	footer->addWidget(buttons);
	root->addLayout(footer);

	syncControls();
}

void FunctionGeneratorDialog::setGenerator(const FunctionGenerator& generator)
{
	m_gen = generator;
	syncControls();
}

void FunctionGeneratorDialog::setShape(FunctionGenerator::Shape shape)
{
	m_gen.shape = shape;
	syncControls();
}

void FunctionGeneratorDialog::toggleExpression(bool on)
{
	if (m_syncing) { return; }

	if (on)
	{
		if (m_gen.shape != FunctionGenerator::Shape::Custom) { m_shapeBeforeExpression = m_gen.shape; }
		m_gen.shape = FunctionGenerator::Shape::Custom;
	}
	else if (m_gen.shape == FunctionGenerator::Shape::Custom) { m_gen.shape = m_shapeBeforeExpression; }
	syncControls();
	if (on) { m_expression->setFocus(); }
}

void FunctionGeneratorDialog::expressionChanged()
{
	if (m_syncing) { return; }
	m_gen.expression = m_expression->text().toStdString();
	syncControls();
}

void FunctionGeneratorDialog::reseed()
{
	static std::mt19937 rng{std::random_device{}()};
	m_gen.seed = static_cast<unsigned>(rng());
	syncControls();
}

void FunctionGeneratorDialog::resetParams()
{
	const auto shape = m_gen.shape;
	m_gen = FunctionGenerator{};
	m_gen.shape = shape;
	m_gen.low = m_rangeMin;
	m_gen.high = m_rangeMax;
	syncControls();
}

void FunctionGeneratorDialog::paramsChanged()
{
	if (m_syncing) { return; }

	m_gen.cycles = m_cycles->value();
	m_gen.phase = m_phase->value() / 360.0;
	m_gen.symmetry = m_symmetry->value() / 100.0;
	m_gen.bend = m_bend->value() / 100.0;
	m_gen.steps = static_cast<int>(std::round(m_steps->value()));
	m_gen.cells = static_cast<int>(std::round(m_cells->value()));
	m_gen.detail = static_cast<int>(std::round(m_detail->value()));
	m_gen.low = static_cast<float>(m_low->value());
	m_gen.high = static_cast<float>(m_high->value());
	m_gen.invert = m_invert->isChecked();
	m_gen.frequencyRange = m_freqRange->value();
	m_gen.amplitude = m_amplitude->curve();
	m_gen.frequency = m_frequency->curve();
	syncControls();
}

void FunctionGeneratorDialog::syncControls()
{
	m_syncing = true;

	for (auto button : m_shapeButtons) { button->setChecked(button->shape() == m_gen.shape); }
	if (m_gen.shape != FunctionGenerator::Shape::Custom) { m_shapeBeforeExpression = m_gen.shape; }

	m_cycles->setValue(m_gen.cycles);
	m_phase->setValue(m_gen.phase * 360.0);
	m_symmetry->setValue(m_gen.symmetry * 100.0);
	m_bend->setValue(m_gen.bend * 100.0);
	m_steps->setValue(m_gen.steps);
	m_cells->setValue(m_gen.cells);
	m_detail->setValue(m_gen.detail);
	m_low->setValue(m_gen.low);
	m_high->setValue(m_gen.high);
	m_invert->setChecked(m_gen.invert);
	m_freqRange->setValue(m_gen.frequencyRange);
	m_amplitude->setCurve(m_gen.amplitude);
	m_frequency->setCurve(m_gen.frequency);
	m_freqRange->setEnabled(m_gen.frequencyActive());

	const bool custom = m_gen.shape == FunctionGenerator::Shape::Custom;
	m_expressionToggle->setChecked(custom);
	m_expressionToggle->setArrowType(custom ? Qt::DownArrow : Qt::RightArrow);
	m_expressionBox->setVisible(custom);
	if (m_expression->text().toStdString() != m_gen.expression) { m_expression->setText(QString::fromStdString(m_gen.expression)); }
	const std::string error = m_gen.expressionError();
	m_expressionStatus->setText(error.empty()
		? tr("p = position in cycles, t = 0\u20261 over the clip, n = cycle number. Result from \u22121 to 1. "
			 "Functions: sin cos abs sqrt pow min max mix fract noise \u2026")
		: tr("Error: %1").arg(QString::fromStdString(error)));
	m_expressionStatus->setEnabled(error.empty());

	m_symmetry->setLabel(m_gen.shape == FunctionGenerator::Shape::Square ? tr("Pulse width") : tr("Symmetry"));
	m_symmetry->setEnabled(m_gen.usesSymmetry());
	m_bend->setEnabled(m_gen.usesBend());
	m_cells->setEnabled(m_gen.usesCells());
	m_reseedButton->setEnabled(m_gen.usesCells());
	m_detail->setEnabled(m_gen.shape == FunctionGenerator::Shape::Sine || m_gen.shape == FunctionGenerator::Shape::SmoothNoise
		|| m_gen.shape == FunctionGenerator::Shape::Custom || m_gen.bend != 0.0 || m_gen.steps > 1
		|| m_gen.amplitudeActive());

	m_syncing = false;

	auto nodes = m_gen.build(m_lengthTicks);
	m_info->setText(tr("%n node(s)", "", static_cast<int>(nodes.size())));
	m_preview->setData(std::move(nodes), m_gen.cycles, m_gen.phase, m_lengthTicks, m_rangeMin, m_rangeMax);
}

} // namespace lmms::gui

#include "FunctionGeneratorDialog.moc"
