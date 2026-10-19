/*
 * TimeLineWidget.cpp - class timeLine, representing a time-line with position marker
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

#include "TimeLineWidget.h"

#include <algorithm>
#include <cmath>

#include <QGuiApplication>
#include <QInputDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QToolBar>

#include "ConfigManager.h"
#include "DeprecationHelper.h"
#include "embed.h"
#include "FontHelper.h"
#include "KeyboardShortcuts.h"
#include "NStateButton.h"
#include "TextFloat.h"
#include "Song.h"

namespace lmms::gui
{

namespace
{
	constexpr int MIN_BAR_LABEL_DISTANCE = 35;

	struct SectionPreset { const char* name; QColor color; };
	const std::array<SectionPreset, 8>& sectionPresets()
	{
		static const std::array<SectionPreset, 8> presets{{
			{QT_TRANSLATE_NOOP("lmms::gui::TimeLineWidget", "Intro"), QColor(0x5b, 0x8d, 0xef)},
			{QT_TRANSLATE_NOOP("lmms::gui::TimeLineWidget", "Verse"), QColor(0x3f, 0xbf, 0x8f)},
			{QT_TRANSLATE_NOOP("lmms::gui::TimeLineWidget", "Pre-Chorus"), QColor(0xd4, 0xa9, 0x3b)},
			{QT_TRANSLATE_NOOP("lmms::gui::TimeLineWidget", "Chorus"), QColor(0xe0, 0x54, 0x4e)},
			{QT_TRANSLATE_NOOP("lmms::gui::TimeLineWidget", "Bridge"), QColor(0x9b, 0x6b, 0xd6)},
			{QT_TRANSLATE_NOOP("lmms::gui::TimeLineWidget", "Breakdown"), QColor(0x4f, 0xb3, 0xc9)},
			{QT_TRANSLATE_NOOP("lmms::gui::TimeLineWidget", "Drop"), QColor(0xff, 0x7a, 0x3d)},
			{QT_TRANSLATE_NOOP("lmms::gui::TimeLineWidget", "Outro"), QColor(0x7d, 0x87, 0x96)},
		}};
		return presets;
	}

	QIcon swatch(const QColor& color)
	{
		QPixmap pixmap(12, 12);
		pixmap.fill(Qt::transparent);
		QPainter p(&pixmap);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(color.darker(160));
		p.setBrush(color);
		p.drawRect(QRectF(0.5, 0.5, 11, 11));
		return pixmap;
	}

	//! Bold label font that grows with the marker lane (whose height follows the theme's font size)
	QFont markerFont(QFont font, int laneHeight)
	{
		font = adjustedToPixelSize(font, std::clamp(static_cast<int>(laneHeight * 0.6), 10, 18));
		font.setBold(true);
		return font;
	}

	//! WCAG relative luminance
	double luminance(const QColor& color)
	{
		const auto channel = [](double v)
		{
			v /= 255.0;
			return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
		};
		return 0.2126 * channel(color.red()) + 0.7152 * channel(color.green()) + 0.0722 * channel(color.blue());
	}

	double contrast(const QColor& a, const QColor& b)
	{
		const auto la = luminance(a);
		const auto lb = luminance(b);
		return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
	}

	struct TagColors { QColor fill; QColor text; };

	//! Picks the label color that reads best on @p fill and, where even that stays below 4.5:1 (any custom
	//! color can be chosen), shades the tag until it doesn't
	TagColors readableTag(QColor fill)
	{
		const auto light = QColor(255, 255, 255);
		const auto dark = QColor(20, 22, 26);
		const bool useDark = contrast(fill, dark) >= contrast(fill, light);
		const auto text = useDark ? dark : light;
		for (int i = 0; i < 20 && contrast(fill, text) < 4.5; ++i)
		{
			fill = useDark ? fill.lighter(108) : fill.darker(108);
		}
		return {fill, text};
	}
}

TimeLineWidget::TimeLineWidget(const int xoff, const int yoff, const float ppb, Timeline& timeline,
		const TimePos& begin, QWidget* parent) :
	QWidget{parent},
	m_xOffset{xoff},
	m_ppb{ppb},
	m_timeline{&timeline},
	m_begin{begin}
{
	move( 0, yoff );

	setMouseTracking(true);

	connect( Engine::getSong(), SIGNAL(timeSignatureChanged(int,int)),
					this, SLOT(update()));
	connect(m_timeline, &Timeline::positionChanged, this, qOverload<>(&QWidget::update));
	connect(m_timeline, &Timeline::markersChanged, this, qOverload<>(&QWidget::update));
}




TimeLineWidget::~TimeLineWidget()
{
	delete m_hint;
}



void TimeLineWidget::setXOffset(const int x)
{
	m_xOffset = x;
}

void TimeLineWidget::addToolButtons( QToolBar * _tool_bar )
{
	auto autoScroll = new NStateButton(_tool_bar);
	autoScroll->setGeneralToolTip(tr("Auto scrolling"));
	autoScroll->addState(embed::getIconPixmap("autoscroll_stepped_on"), tr("Stepped auto scrolling"));
	autoScroll->addState(embed::getIconPixmap("autoscroll_continuous_on"), tr("Continuous auto scrolling"));
	autoScroll->addState(embed::getIconPixmap("autoscroll_off"), tr("Auto scrolling disabled"));
	autoScroll->changeState(static_cast<int>(m_autoScroll));
	connect( autoScroll, SIGNAL(changedState(int)), this,
					SLOT(toggleAutoScroll(int)));

	auto loopPoints = new NStateButton(_tool_bar);
	loopPoints->setGeneralToolTip( tr( "Loop points" ) );
	loopPoints->addState( embed::getIconPixmap( "loop_points_off" ) );
	loopPoints->addState( embed::getIconPixmap( "loop_points_on" ) );
	connect(loopPoints, &NStateButton::changedState, m_timeline, &Timeline::setLoopEnabled);
	connect(m_timeline, &Timeline::loopEnabledChanged, loopPoints, &NStateButton::changeState);
	connect(m_timeline, &Timeline::loopEnabledChanged, this, static_cast<void (QWidget::*)()>(&QWidget::update));
	loopPoints->changeState(static_cast<int>(m_timeline->loopEnabled()));

	auto behaviourAtStop = new NStateButton(_tool_bar);
	behaviourAtStop->addState( embed::getIconPixmap( "back_to_zero" ),
					tr( "After stopping go back to beginning" )
									);
	behaviourAtStop->addState( embed::getIconPixmap( "back_to_start" ),
					tr( "After stopping go back to "
						"position at which playing was "
						"started" ) );
	behaviourAtStop->addState( embed::getIconPixmap( "keep_stop_position" ),
					tr( "After stopping keep position" ) );
	connect(behaviourAtStop, &NStateButton::changedState, m_timeline,
		[timeline = m_timeline](int value) {
			timeline->setStopBehaviour(static_cast<Timeline::StopBehaviour>(value));
		}
	);
	connect(m_timeline, &Timeline::stopBehaviourChanged, behaviourAtStop,
		[button = behaviourAtStop](Timeline::StopBehaviour value) {
			button->changeState(static_cast<int>(value));
		}
	);
	behaviourAtStop->changeState(static_cast<int>(m_timeline->stopBehaviour()));

	_tool_bar->addWidget( autoScroll );
	_tool_bar->addWidget( loopPoints );
	_tool_bar->addWidget( behaviourAtStop );
}

void TimeLineWidget::toggleAutoScroll( int _n )
{
	m_autoScroll = static_cast<AutoScrollState>( _n );
}

void TimeLineWidget::paintEvent( QPaintEvent * )
{
	QPainter p( this );

	// Draw background
	p.fillRect( 0, 0, width(), height(), p.background() );

	// Arrangement markers get a lane along the top; everything else moves below it
	const int lane = markerLaneHeight();
	if (lane > 0) { paintMarkers(p); }
	const int height = this->height() - lane;
	p.translate(0, lane);

	// Clip so that we only draw everything starting from the offset
	p.setClipRect(m_xOffset, 0, width() - m_xOffset, height);

	// Variables for the loop rectangle
	int const loopRectMargin = getLoopRectangleVerticalPadding();
	int const loopRectHeight = height - 2 * loopRectMargin;
	int const loopStart = markerX(m_timeline->loopBegin());
	int const loopEndR = markerX(m_timeline->loopEnd());
	int const loopRectWidth = loopEndR - loopStart;

	bool const loopPointsActive = m_timeline->loopEnabled();

	// Draw the main loop rectangle (inner fill only)
	QRect outerRectangle( loopStart, loopRectMargin, loopRectWidth - 1, loopRectHeight - 1 );
	p.fillRect( outerRectangle, loopPointsActive ? getActiveLoopBrush() : getInactiveLoopBrush());

	// Draw the bar lines and numbers
	// Activate hinting on the font
	QFont font = p.font();
	font.setHintingPreference( QFont::PreferFullHinting );
	p.setFont(font);
	int const fontAscent = p.fontMetrics().ascent();
	int const fontHeight = p.fontMetrics().height();

	QColor const & barLineColor = getBarLineColor();
	QColor const & barNumberColor = getBarNumberColor();

	bar_t barNumber = m_begin.getBar();
	int const x = m_xOffset - ((static_cast<int>(m_begin * m_ppb) / TimePos::ticksPerBar()) % static_cast<int>(m_ppb));

	// Double the interval between bar numbers until they are far enough apart
	int barLabelInterval = 1;
	while (barLabelInterval * m_ppb < MIN_BAR_LABEL_DISTANCE) { barLabelInterval *= 2; }

	for( int i = 0; x + i * m_ppb < width(); ++i )
	{
		++barNumber;
		if ((barNumber - 1) % barLabelInterval == 0)
		{
			const int cx = x + qRound( i * m_ppb );
			p.setPen( barLineColor );
			p.drawLine( cx, 5, cx, height - 6 );

			const QString s = QString::number( barNumber );
			p.setPen( barNumberColor );
			p.drawText( cx + 5, ((height - fontHeight) / 2) + fontAscent, s );
		}
	}

	// Draw the loop rectangle's outer outline
	p.setPen( loopPointsActive ? getActiveLoopColor() : getInactiveLoopColor() );
	p.setBrush( Qt::NoBrush );
	p.drawRect( outerRectangle );

	// Draw the loop rectangle's inner outline
	QRect innerRectangle = outerRectangle.adjusted( 1, 1, -1, -1 );
	p.setPen( loopPointsActive ? getActiveLoopInnerColor() : getInactiveLoopInnerColor() );
	p.setBrush( Qt::NoBrush );
	p.drawRect( innerRectangle );
	
	// Draw loop handles if necessary
	const auto handleMode = ConfigManager::inst()->value("app", "loopmarkermode") == "handles";
	if (handleMode && underMouse() && QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier))
	{
		const auto handleWidth = std::min(m_loopHandleWidth, loopRectWidth / 2 - 1);
		const auto leftHandle = QRectF(loopStart - .5, loopRectMargin - .5, handleWidth, loopRectHeight);
		const auto rightHandle = QRectF(loopEndR - handleWidth - .5, loopRectMargin - .5, handleWidth, loopRectHeight);
		const auto color = loopPointsActive ? m_activeLoopHandleColor : m_inactiveLoopHandleColor;

		p.fillRect(leftHandle, color);
		p.fillRect(rightHandle, color);
	}

	const QPixmap& marker = !m_isRecording ? m_posMarkerPixmap : m_recordingPosMarkerPixmap;

	// Only draw the position marker if the position line is in view
	if (m_isPlayheadVisible && markerX(m_timeline->pos()) >= m_xOffset && markerX(m_timeline->pos()) < width() - marker.width() / 2)
	{
		// Let the position marker extrude to the left
		p.setClipping(false);
		p.setOpacity(0.6);
		p.drawPixmap(markerX(m_timeline->pos()) - (marker.width() / 2),
			height - marker.height(), marker);
	}
}

auto TimeLineWidget::getClickedTime(const int xPosition) const -> TimePos
{
	// How far into the timeline we clicked, measuring pixels from the leftmost part of the editor
	const auto pixelDelta = std::max(xPosition - m_xOffset, 0);
	return m_begin + static_cast<int>(pixelDelta * TimePos::ticksPerBar() / m_ppb);
}

auto TimeLineWidget::getLoopAction(QMouseEvent* event) const -> TimeLineWidget::Action
{
	const auto pos = position(event);

	const auto mode = ConfigManager::inst()->value("app", "loopmarkermode");
	const auto xPos = pos.x();
	const auto button = event->button();

	if (mode == "handles")
	{
		// Loop start and end pos, or closest edge of screen if loop extends off it
		const auto leftMost = std::max(markerX(m_timeline->loopBegin()), m_xOffset);
		const auto rightMost = std::min(markerX(m_timeline->loopEnd()), width());
		// Distance from click to handle, positive aimed towards center of loop
		const auto deltaLeft = xPos - leftMost;
		const auto deltaRight = rightMost - xPos;

		if (deltaLeft < 0 || deltaRight < 0) { return Action::NoAction; } // Clicked outside loop
		else if (deltaLeft <= m_loopHandleWidth && deltaLeft < deltaRight) { return Action::MoveLoopBegin; }
		else if (deltaRight <= m_loopHandleWidth) { return Action::MoveLoopEnd; }
		else { return Action::MoveLoop; }
	}
	else if (mode == "closest")
	{
		const TimePos loopMid = (m_timeline->loopBegin() + m_timeline->loopEnd()) / 2;
		return getClickedTime(xPos) < loopMid ? Action::MoveLoopBegin : Action::MoveLoopEnd;
	}
	else // Default to dual-button mode
	{
		if (button == Qt::LeftButton) { return Action::MoveLoopBegin; }
		else if (button == Qt::RightButton) { return Action::MoveLoopEnd; }
		return Action::NoAction;
	}
}

auto TimeLineWidget::actionCursor(Action action) const -> QCursor
{
	switch (action) {
		case Action::MoveLoop: return Qt::SizeHorCursor;
		case Action::MoveLoopBegin: return m_cursorSelectLeft;
		case Action::MoveLoopEnd: return m_cursorSelectRight;
		// Fall back to normal cursor if no action or action cursor not specified
		default: return Qt::ArrowCursor;
	}
}

void TimeLineWidget::mousePressEvent(QMouseEvent* event)
{
	const auto pos = position(event);

	if (pos.x() < m_xOffset) { return; }

	// Grab a marker to move it, or click it to jump there
	if (event->button() == Qt::LeftButton && !(event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)))
	{
		if (const int marker = markerAt(QPoint(pos.x(), pos.y())); marker >= 0)
		{
			m_action = Action::MoveMarker;
			m_dragMarker = marker;
			m_markerMoved = false;
			m_dragStartPos = getClickedTime(pos.x());
			m_markerDragOrigin = m_timeline->markers()[marker].pos;
			setCursor(Qt::ClosedHandCursor);
			setContextMenuPolicy(Qt::PreventContextMenu);
			return;
		}
	}

	const auto shift = event->modifiers() & Qt::ShiftModifier;
	const auto ctrl = event->modifiers() & Qt::ControlModifier;

	if (shift) // loop marker manipulation
	{
		m_action = getLoopAction(event);
		setCursor(actionCursor(m_action));

		if (m_action == Action::MoveLoop)
		{
			m_dragStartPos = getClickedTime(pos.x());
			m_oldLoopPos = {m_timeline->loopBegin(), m_timeline->loopEnd()};
		}
	}
	else if (event->button() == Qt::LeftButton && ctrl) // selection
	{
		m_action = Action::SelectSongClip;
		m_initalXSelect = pos.x();
	}
	else if (event->button() == Qt::LeftButton && !ctrl) // move playhead
	{
		m_action = Action::MovePositionMarker;
	}

	if (m_action == Action::MoveLoopBegin || m_action == Action::MoveLoopEnd)
	{
		delete m_hint;
		m_hint = TextFloat::displayMessage(tr("Hint"),
			tr("Press <%1> to disable magnetic loop points.").arg(UI_CTRL_KEY),
			embed::getIconPixmap("hint"), 0);
	}

	setContextMenuPolicy(m_action == Action::NoAction ? Qt::DefaultContextMenu : Qt::PreventContextMenu);

	mouseMoveEvent(event);
}

void TimeLineWidget::mouseMoveEvent( QMouseEvent* event )
{
	parentWidget()->update(); // essential for widgets that this timeline had taken their mouse move event from.

	const auto pos = position(event);

	auto timeAtCursor = getClickedTime(pos.x());
	const auto control = event->modifiers() & Qt::ControlModifier;

	switch( m_action )
	{
		case Action::MovePositionMarker:
			m_timeline->setTicks(timeAtCursor.getTicks());
			if (!( Engine::getSong()->isPlaying()))
			{
				//Song::PlayMode::None is used when nothing is being played.
				Engine::getSong()->getTimeline(Song::PlayMode::None).setTicks(timeAtCursor.getTicks());
			}
			update();
			break;

		case Action::MoveLoopBegin:
		case Action::MoveLoopEnd:
		{
			const auto otherPoint = m_action == Action::MoveLoopBegin
				? m_timeline->loopEnd()
				: m_timeline->loopBegin();
			if (control)
			{
				// no ctrl-press-hint when having ctrl pressed
				delete m_hint;
				m_hint = nullptr;
			}
			else
			{
				timeAtCursor = timeAtCursor.quantize(m_snapSize);
			}
			// Catch begin == end
			if (timeAtCursor == otherPoint)
			{
				const int offset = control ? 1 : m_snapSize * TimePos::ticksPerBar();
				if (m_action == Action::MoveLoopBegin) { timeAtCursor -= offset; }
				else { timeAtCursor += offset; }
			}
			// Update m_action so we still move the correct point even if it is
			// dragged past the other.
			m_action = timeAtCursor < otherPoint ? Action::MoveLoopBegin : Action::MoveLoopEnd;
			m_timeline->setLoopPoints(timeAtCursor, otherPoint);
			update();
			break;
		}
		case Action::MoveLoop:
		{
			const TimePos dragDelta = timeAtCursor - m_dragStartPos;
			auto loopPos = m_oldLoopPos;
			for (auto& point : loopPos)
			{
				point += dragDelta;
				if (!control) { point = point.quantize(m_snapSize); }
			}
			m_timeline->setLoopPoints(loopPos[0], loopPos[1]);
			break;
		}
		case Action::SelectSongClip:
			emit regionSelectedFromPixels(m_initalXSelect, pos.x());
			break;

		case Action::MoveMarker:
		{
			auto target = TimePos{m_markerDragOrigin + (timeAtCursor - m_dragStartPos)};
			if (!control) { target = target.quantize(m_snapSize); }
			if (m_dragMarker >= 0 && m_dragMarker < static_cast<int>(m_timeline->markers().size())
				&& target != m_timeline->markers()[m_dragMarker].pos)
			{
				if (!m_markerMoved) { m_timeline->addJournalCheckPoint(); }
				m_markerMoved = true;
				m_dragMarker = m_timeline->moveMarker(m_dragMarker, target);
			}
			break;
		}

		default:
			break;
	}

	if (event->buttons() == Qt::NoButton)
	{
		const int hover = markerAt(QPoint(pos.x(), pos.y()));
		if (hover != m_hoverMarker)
		{
			m_hoverMarker = hover;
			update();
		}
		setCursor(QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier)
			? actionCursor(getLoopAction(event))
			: hover >= 0 ? Qt::OpenHandCursor : Qt::ArrowCursor
		);
		if (hover >= 0)
		{
			setToolTip(tr("%1 - drag to move, double-click to rename, right-click for options")
				.arg(m_timeline->markers()[hover].name));
		}
		else if (markerLaneHeight() > 0 && pos.y() < markerLaneHeight())
		{
			setToolTip(tr("Marker lane - double-click or right-click to add a marker"));
		}
		else { setToolTip(QString()); }
	}
}

void TimeLineWidget::mouseReleaseEvent( QMouseEvent* event )
{
	delete m_hint;
	m_hint = nullptr;
	if ( m_action == Action::SelectSongClip ) { emit selectionFinished(); }
	if (m_action == Action::MoveMarker)
	{
		// A click without a drag jumps to the marker
		if (!m_markerMoved && m_dragMarker >= 0 && m_dragMarker < static_cast<int>(m_timeline->markers().size()))
		{
			jumpTo(m_timeline->markers()[m_dragMarker].pos);
		}
		m_dragMarker = -1;
		setCursor(Qt::OpenHandCursor);
		setContextMenuPolicy(Qt::DefaultContextMenu);
	}
	m_action = Action::NoAction;
}

void TimeLineWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
	const auto pos = position(event);
	if (event->button() != Qt::LeftButton || pos.x() < m_xOffset) { return; }
	if (const int marker = markerAt(QPoint(pos.x(), pos.y())); marker >= 0)
	{
		renameMarker(marker);
	}
	else if (markerLaneHeight() > 0 && pos.y() < markerLaneHeight())
	{
		renameMarker(addMarker(getClickedTime(pos.x())));
	}
}

void TimeLineWidget::leaveEvent(QEvent* event)
{
	if (m_hoverMarker >= 0)
	{
		m_hoverMarker = -1;
		update();
	}
	QWidget::leaveEvent(event);
}

void TimeLineWidget::contextMenuEvent(QContextMenuEvent* event)
{
	if (event->x() < m_xOffset) { return; }

	auto menu = QMenu{};

	if (markerLaneHeight() > 0 && event->y() < markerLaneHeight())
	{
		if (const int marker = markerAt(event->pos()); marker >= 0)
		{
			menu.addAction(tr("Rename marker..."), [this, marker] { renameMarker(marker); });
			const auto colors = menu.addMenu(tr("Color"));
			for (const auto& preset : sectionPresets())
			{
				colors->addAction(swatch(preset.color), tr(preset.name), [this, marker, color = preset.color] {
					m_timeline->addJournalCheckPoint();
					m_timeline->setMarkerColor(marker, color);
				});
			}
			menu.addAction(tr("Loop this section"), [this, marker] {
				const auto begin = m_timeline->markers()[marker].pos;
				auto end = m_timeline->sectionEnd(marker);
				if (end <= begin) { end = std::max<tick_t>(begin + 4 * TimePos::ticksPerBar(), TimePos(Engine::getSong()->length(), 0)); }
				m_timeline->setLoopPoints(begin, end);
				m_timeline->setLoopEnabled(true);
				update();
			});
			menu.addAction(tr("Move playhead here"), [this, marker] { jumpTo(m_timeline->markers()[marker].pos); });
			menu.addSeparator();
			menu.addAction(embed::getIconPixmap("cancel"), tr("Remove marker"), [this, marker] {
				m_timeline->addJournalCheckPoint();
				m_timeline->removeMarker(marker);
			});
		}
		else
		{
			auto at = getClickedTime(event->x());
			if (!QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier)) { at = at.quantize(m_snapSize); }
			const auto add = menu.addMenu(tr("Add marker here"));
			for (const auto& preset : sectionPresets())
			{
				add->addAction(swatch(preset.color), tr(preset.name), [this, at, &preset] {
					addMarker(at, tr(preset.name), preset.color);
				});
			}
			add->addSeparator();
			add->addAction(tr("Custom..."), [this, at] { renameMarker(addMarker(at)); });
			if (!m_timeline->markers().empty())
			{
				menu.addAction(tr("Remove all markers"), [this] {
					m_timeline->addJournalCheckPoint();
					m_timeline->clearMarkers();
				});
			}
		}
		menu.exec(event->globalPos());
		return;
	}

	menu.addAction(tr("Set loop begin here"), [this, event] {
		auto begin = getClickedTime(event->x());
		const auto end = m_timeline->loopEnd();
		if (!QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier)) { begin = begin.quantize(m_snapSize); }
		if (begin == end) { m_timeline->setLoopEnd(end + m_snapSize * TimePos::ticksPerBar()); }
		m_timeline->setLoopBegin(begin);
		update();
	});
	menu.addAction(tr("Set loop end here"), [this, event] {
		const auto begin = m_timeline->loopBegin();
		auto end = getClickedTime(event->x());
		if (!QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier)) { end = end.quantize(m_snapSize); }
		if (begin == end) { m_timeline->setLoopBegin(begin - m_snapSize * TimePos::ticksPerBar()); }
		m_timeline->setLoopEnd(end);
		update();
	});

	menu.addSeparator();

	const auto loopMenu = menu.addMenu(tr("Loop edit mode (hold shift)"));
	const auto loopMode = ConfigManager::inst()->value("app", "loopmarkermode", "dual");
	const auto addLoopModeAction = [loopMenu, &loopMode](QString text, QString mode) {
		const auto action = loopMenu->addAction(text, [mode] {
			ConfigManager::inst()->setValue("app", "loopmarkermode", mode);
		});
		action->setCheckable(true);
		if (loopMode == mode) { action->setChecked(true); }
	};
	addLoopModeAction(tr("Dual-button"), "dual");
	addLoopModeAction(tr("Grab closest"), "closest");
	addLoopModeAction(tr("Handles"), "handles");

	menu.exec(event->globalPos());
}


void TimeLineWidget::setMarkersEnabled(bool enabled)
{
	m_markersEnabled = enabled;
	update();
}

int TimeLineWidget::markerLaneHeight() const
{
	return m_markersEnabled ? static_cast<int>(height() * 0.46) : 0;
}

int TimeLineWidget::addMarker(TimePos pos, const QString& name, const QColor& color)
{
	const auto count = m_timeline->markers().size();
	const auto& presets = sectionPresets();
	m_timeline->addJournalCheckPoint();
	return m_timeline->addMarker(pos,
		name.isEmpty() ? tr("Marker %1").arg(count + 1) : name,
		color.isValid() ? color : presets[count % presets.size()].color);
}

void TimeLineWidget::jumpToNextMarker()
{
	if (const int next = m_timeline->nextMarker(m_timeline->pos()); next >= 0)
	{
		jumpTo(m_timeline->markers()[next].pos);
	}
}

void TimeLineWidget::jumpToPreviousMarker()
{
	// While playing, give a moment of grace so pressing twice skips back past the marker just passed
	auto pos = m_timeline->pos();
	if (Engine::getSong()->isPlaying()) { pos -= TimePos::ticksPerBar() / 4; }
	const int previous = m_timeline->previousMarker(pos);
	jumpTo(previous >= 0 ? m_timeline->markers()[previous].pos : TimePos{0});
}

void TimeLineWidget::jumpTo(TimePos pos)
{
	m_timeline->setTicks(pos.getTicks());
	if (!Engine::getSong()->isPlaying())
	{
		Engine::getSong()->getTimeline(Song::PlayMode::None).setTicks(pos.getTicks());
	}
	parentWidget()->update();
	update();
}

void TimeLineWidget::renameMarker(int index)
{
	if (index < 0 || index >= static_cast<int>(m_timeline->markers().size())) { return; }
	bool ok = false;
	const auto name = QInputDialog::getText(this, tr("Rename marker"), tr("Marker name:"), QLineEdit::Normal,
		m_timeline->markers()[index].name, &ok).trimmed();
	if (!ok || name.isEmpty()) { return; }
	m_timeline->addJournalCheckPoint();
	m_timeline->renameMarker(index, name);
}

auto TimeLineWidget::markerTagRect(int index) const -> QRect
{
	const auto& markers = m_timeline->markers();
	const int lane = markerLaneHeight();
	const int x = markerX(markers[index].pos);
	const QFontMetrics metrics(markerFont(font(), lane));
	int width = metrics.horizontalAdvance(markers[index].name) + 12;
	// Don't run into the next section's tag
	const auto end = m_timeline->sectionEnd(index);
	if (end >= TimePos{0}) { width = std::min(width, std::max(10, markerX(end) - x - 2)); }
	return QRect(x, 1, std::min(width, 220), lane - 2);
}

auto TimeLineWidget::markerAt(const QPoint& pos) const -> int
{
	if (pos.y() >= markerLaneHeight() || pos.x() < m_xOffset) { return -1; }
	const auto& markers = m_timeline->markers();
	// Later tags are drawn on top, so check them first
	for (auto i = static_cast<int>(markers.size()) - 1; i >= 0; --i)
	{
		if (markerTagRect(i).adjusted(-2, 0, 0, 0).contains(pos)) { return i; }
	}
	return -1;
}

void TimeLineWidget::paintMarkers(QPainter& p)
{
	const int lane = markerLaneHeight();
	const auto& markers = m_timeline->markers();

	p.save();
	p.setClipRect(m_xOffset, 0, width() - m_xOffset, height());
	p.fillRect(QRect(m_xOffset, 0, width() - m_xOffset, lane), QColor(0, 0, 0, 70));
	p.setPen(QColor(255, 255, 255, 18));
	p.drawLine(m_xOffset, lane - 1, width(), lane - 1);

	if (markers.empty())
	{
		p.setFont(adjustedToPixelSize(font(), 9));
		p.setPen(QColor(255, 255, 255, 60));
		p.drawText(QRect(m_xOffset + 6, 0, width() - m_xOffset - 12, lane), Qt::AlignVCenter | Qt::AlignLeft,
			tr("Double-click to add a marker"));
	}

	// Tint each section of the timeline in its marker's color
	for (std::size_t i = 0; i < markers.size(); ++i)
	{
		const auto end = m_timeline->sectionEnd(static_cast<int>(i));
		const int x = markerX(markers[i].pos);
		const int right = end >= TimePos{0} ? markerX(end) : width();
		if (right <= m_xOffset || x >= width()) { continue; }
		QColor tint = markers[i].color;
		tint.setAlpha(26);
		p.fillRect(QRect(x, 0, right - x, height()), tint);
	}

	p.setRenderHint(QPainter::Antialiasing);
	p.setFont(markerFont(font(), lane));
	for (std::size_t i = 0; i < markers.size(); ++i)
	{
		const auto& marker = markers[i];
		const QRect tag = markerTagRect(static_cast<int>(i));
		if (tag.right() < m_xOffset || tag.left() > width()) { continue; }
		const bool hot = static_cast<int>(i) == m_hoverMarker || static_cast<int>(i) == m_dragMarker;
		const auto colors = readableTag(hot ? marker.color.lighter(125) : marker.color);

		// Flag pole through the whole timeline, then the tag
		QColor pole = marker.color;
		pole.setAlpha(200);
		p.setPen(QPen(pole, 1));
		p.drawLine(QPointF(tag.left() + 0.5, 0), QPointF(tag.left() + 0.5, height()));
		p.fillRect(tag, colors.fill);

		p.setPen(colors.text);
		const QRect text = tag.adjusted(6, 0, -4, 0);
		if (text.width() > 6)
		{
			p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft,
				QFontMetrics(p.font()).elidedText(marker.name, Qt::ElideRight, text.width()));
		}
	}
	p.restore();
}


TimeLineWidget::AutoScrollState TimeLineWidget::defaultAutoScrollState()
{
	QString autoScrollState = ConfigManager::inst()->value("ui", "autoscroll");
	if (autoScrollState == AutoScrollSteppedString) { return AutoScrollState::Stepped; }
	else if (autoScrollState == AutoScrollContinuousString) { return AutoScrollState::Continuous; }
	else if (autoScrollState == AutoScrollDisabledString) { return AutoScrollState::Disabled; }
	else { return AutoScrollState::Stepped; }
}


} // namespace lmms::gui
