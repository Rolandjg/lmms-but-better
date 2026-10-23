/*
 * AutomationClipView.cpp - implementation of view for AutomationClip
 *
 * Copyright (c) 2008-2010 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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
#include "AutomationClipView.h"

#include <QActionGroup>
#include <QApplication>
#include <QInputDialog>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>  // IWYU pragma: keep
#include <QMenu>
#include <cmath>
#include <tuple>

#include "AutomationEditor.h"
#include "DeprecationHelper.h"
#include "embed.h"
#include "GuiApplication.h"
#include "KeyboardShortcuts.h"
#include "ProjectJournal.h"
#include "RenameDialog.h"
#include "Song.h"
#include "StringPairDrag.h"
#include "TextFloat.h"
#include "Track.h"
#include "TrackContainerView.h"
#include "TrackView.h"

#include "Engine.h"


namespace lmms::gui
{

namespace
{

constexpr int NodeRadius = 3;
constexpr int NodeHitRadius = 6;
constexpr int CurveHitDistance = 4;
//! Below this height there is no room to place values with the mouse
constexpr int MinHeightForNodeEditing = 16;
//! Dragging a point over this many clip heights sweeps its whole value range, for finer control
constexpr float ValueDragScale = 3.f;

} // namespace

AutomationClipView::AutomationClipView( AutomationClip * _clip,
						TrackView * _parent ) :
	ClipView( _clip, _parent ),
	m_clip( _clip ),
	m_paintPixmap()
{
	connect( m_clip, SIGNAL(dataChanged()),
			this, SLOT(update()));
	connect( getGUI()->automationEditor(), SIGNAL(currentClipChanged()),
			this, SLOT(update()));

	setToolTip(m_clip->name());
	setStyle( QApplication::style() );
	update();
}




void AutomationClipView::openInAutomationEditor()
{
	if(getGUI() != nullptr)
		getGUI()->automationEditor()->open(m_clip);
}


void AutomationClipView::update()
{
	setToolTip(m_clip->name() + "\n" + tr("Drag a point to move it. Click the line or <Shift>-click to add a point, "
		"right-click a point to remove it, double-click a point to type its value."));

	ClipView::update();
}



void AutomationClipView::resetName()
{
	m_clip->setName( QString() );
}




void AutomationClipView::changeName()
{
	QString s = m_clip->name();
	RenameDialog rename_dlg( s );
	rename_dlg.exec();
	m_clip->setName( s );
	update();
}




void AutomationClipView::disconnectObject( QAction * _a )
{
	JournallingObject * j = Engine::projectJournal()->
				journallingObject( _a->data().toInt() );
	if( j && dynamic_cast<AutomatableModel *>( j ) )
	{
		float oldMin = m_clip->getMin();
		float oldMax = m_clip->getMax();

		m_clip->m_objects.erase( std::find( m_clip->m_objects.begin(),
					m_clip->m_objects.end(),
				dynamic_cast<AutomatableModel *>( j ) ) );
		update();

		//If automation editor is opened, update its display after disconnection
		if( getGUI()->automationEditor() )
		{
			getGUI()->automationEditor()->m_editor->updateAfterClipChange();
		}

		//if there is no more connection connected to the AutomationClip
		if( m_clip->m_objects.size() == 0 )
		{
			//scale the points to fit the new min. and max. value
			this->scaleTimemapToFit( oldMin, oldMax );
		}
	}
}


void AutomationClipView::toggleRecording()
{
	m_clip->setRecording( ! m_clip->isRecording() );
	update();
}




void AutomationClipView::flipY()
{
	m_clip->flipY( m_clip->getMin(), m_clip->getMax() );
	update();
}




void AutomationClipView::flipX()
{
	m_clip->flipX(std::max(0, -m_clip->startTimeOffset()), std::max(0, m_clip->length() - m_clip->startTimeOffset()));
	update();
}




void AutomationClipView::setProgressionType(AutomationClip::ProgressionType type)
{
	if (type == m_clip->progressionType()) { return; }

	m_clip->addJournalCheckPoint();
	m_clip->setProgressionType(type);
	Engine::getSong()->setModified();

	// Keep the Automation Editor's interpolation buttons in sync
	auto editor = getGUI()->automationEditor();
	if (editor && editor->currentClip() == m_clip) { editor->setCurrentClip(m_clip); }
}




void AutomationClipView::constructContextMenu( QMenu * _cm )
{
	auto a = new QAction(embed::getIconPixmap("automation"), tr("Open in Automation editor"), _cm);
	_cm->insertAction( _cm->actions()[0], a );
	connect(a, SIGNAL(triggered()), this, SLOT(openInAutomationEditor()));
	_cm->insertSeparator( _cm->actions()[1] );

	_cm->addSeparator();

	_cm->addAction( embed::getIconPixmap( "edit_erase" ),
			tr( "Clear" ), m_clip, SLOT(clear()));
	_cm->addSeparator();

	_cm->addAction( embed::getIconPixmap( "reload" ), tr( "Reset name" ),
						this, SLOT(resetName()));
	_cm->addAction( embed::getIconPixmap( "edit_rename" ),
						tr( "Change name" ),
						this, SLOT(changeName()));
	_cm->addAction( embed::getIconPixmap( "record" ),
						tr( "Set/clear record" ),
						this, SLOT(toggleRecording()));
	_cm->addAction( embed::getIconPixmap( "flip_y" ),
						tr( "Flip Vertically (Visible)" ),
						this, SLOT(flipY()));
	_cm->addAction( embed::getIconPixmap( "flip_x" ),
						tr( "Flip Horizontally (Visible)" ),
						this, SLOT(flipX()));

	using Progression = AutomationClip::ProgressionType;
	auto progressionMenu = _cm->addMenu(embed::getIconPixmap("progression_linear"), tr("Interpolation"));
	auto progressionGroup = new QActionGroup(progressionMenu);
	for (const auto& [type, icon, text] : {
		std::tuple{Progression::Discrete, "progression_discrete", tr("Discrete")},
		std::tuple{Progression::Linear, "progression_linear", tr("Linear")},
		std::tuple{Progression::CubicHermite, "progression_cubic_hermite", tr("Cubic Hermite")}})
	{
		auto action = progressionMenu->addAction(embed::getIconPixmap(icon), text);
		action->setCheckable(true);
		action->setChecked(m_clip->progressionType() == type);
		progressionGroup->addAction(action);
		connect(action, &QAction::triggered, this, [this, type = type] { setProgressionType(type); });
	}
	
	if (!m_clip->m_objects.empty())
	{
		_cm->addSeparator();
		auto m = new QMenu(tr("%1 Connections").arg(m_clip->m_objects.size()), _cm);
		for (const auto& object : m_clip->m_objects)
		{
			if (object)
			{
				a = new QAction(tr("Disconnect \"%1\"").arg(object->fullDisplayName()), m);
				a->setData(object->id());
				m->addAction( a );
			}
		}
		connect( m, SIGNAL(triggered(QAction*)),
				this, SLOT(disconnectObject(QAction*)));
		_cm->addMenu( m );
	}
}




void AutomationClipView::contextMenuEvent(QContextMenuEvent* cme)
{
	if (m_skipContextMenu && cme->reason() == QContextMenuEvent::Mouse)
	{
		m_skipContextMenu = false;
		cme->accept();
		return;
	}
	ClipView::contextMenuEvent(cme);
}




void AutomationClipView::mousePressEvent(QMouseEvent* me)
{
	m_skipContextMenu = false;
	const auto pos = position(me);

	if (canEditNodes() && !(me->modifiers() & (KBD_COPY_MODIFIER | Qt::ControlModifier)))
	{
		const auto node = nodeAt(pos);
		if (me->button() == Qt::LeftButton && (node || me->modifiers() & Qt::ShiftModifier || isOnCurve(pos)))
		{
			m_clip->addJournalCheckPoint();
			int tick = tickAtX(pos.x());
			float value = valueAtY(pos.y());
			m_dragOffset = QPoint();
			if (node)
			{
				// Grab the node where it is, so it doesn't jump to the cursor
				tick = *node;
				value = m_clip->getTimeMap()[tick].getInValue();
				m_dragOffset = pos - QPointF(xOfTick(tick), yOfValue(value)).toPoint();
			}
			m_dragStartValue = value;
			m_dragStartY = pos.y();
			const bool unquantized = me->modifiers() & Qt::AltModifier;
			m_dragNode = m_clip->setDragValue(TimePos(tick), value, !node && !unquantized, unquantized);
			Engine::getSong()->setModified();
			setCursor(Qt::ClosedHandCursor);
			showNodeValue(*m_dragNode);
			return;
		}
		if (me->button() == Qt::RightButton && node)
		{
			m_clip->addJournalCheckPoint();
			m_clip->removeNode(TimePos(*node));
			Engine::getSong()->setModified();
			m_skipContextMenu = true;
			updateHover(pos);
			return;
		}
	}

	ClipView::mousePressEvent(me);
}




void AutomationClipView::mouseMoveEvent(QMouseEvent* me)
{
	if (m_dragNode)
	{
		const auto pos = position(me) - m_dragOffset;
		const bool unquantized = me->modifiers() & Qt::AltModifier;
		// Points stay within the visible part of the clip
		const int tick = tickAtX(std::clamp(pos.x(), 0, width() - 1));
		// The time follows the mouse, the value moves slower than it
		const float span = std::max(1, height() - 2 * BORDER_WIDTH) * ValueDragScale;
		const float value = m_dragStartValue
			+ (m_dragStartY - position(me).y()) / span * (m_clip->getMax() - m_clip->getMin());
		m_dragNode = m_clip->setDragValue(TimePos(tick), snapValue(value), !unquantized, unquantized);
		Engine::getSong()->setModified();
		showNodeValue(*m_dragNode);
		return;
	}

	ClipView::mouseMoveEvent(me);

	if (!me->buttons()) { updateHover(position(me)); }
}




void AutomationClipView::mouseReleaseEvent(QMouseEvent* me)
{
	if (m_dragNode)
	{
		if (me->button() == Qt::LeftButton)
		{
			m_clip->applyDragValue();
			m_dragNode.reset();
			textFloat()->hide();
			m_hoverNode.reset();
			updateHover(position(me));
			update();
		}
		return;
	}

	ClipView::mouseReleaseEvent(me);
}




void AutomationClipView::mouseDoubleClickEvent( QMouseEvent * me )
{
	if (m_trackView->trackContainerView()->knifeMode()) { return; }

	if(me->button() != Qt::LeftButton)
	{
		me->ignore();
		return;
	}

	// Double-clicking a point lets the user type its exact value
	if (const auto node = canEditNodes() ? nodeAt(position(me)) : std::nullopt)
	{
		const auto model = m_clip->firstObject();
		const float step = model->step<float>();
		const int decimals = step > 0 ? std::clamp(static_cast<int>(std::ceil(-std::log10(step))), 0, 6) : 3;
		const AutomationNode current = m_clip->getTimeMap()[*node];

		bool ok = false;
		const double value = QInputDialog::getDouble(this, tr("Automation point"),
			tr("Value (%1 to %2):").arg(m_clip->getMin()).arg(m_clip->getMax()), current.getInValue(),
			m_clip->getMin(), m_clip->getMax(), decimals, &ok);
		if (!ok || static_cast<float>(value) == current.getInValue()) { return; }

		m_clip->addJournalCheckPoint();
		// A discrete jump at the point is kept
		if (current.getValueOffset() != 0)
		{
			m_clip->putValues(TimePos(*node), value, current.getOutValue(), false);
		}
		else
		{
			m_clip->putValue(TimePos(*node), value, false);
		}
		Engine::getSong()->setModified();
		return;
	}

	openInAutomationEditor();
}




#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
void AutomationClipView::enterEvent(QEnterEvent* e)
#else
void AutomationClipView::enterEvent(QEvent* e)
#endif
{
	m_hovered = true;
	update();
	ClipView::enterEvent(e);
}




void AutomationClipView::leaveEvent(QEvent* e)
{
	m_hovered = false;
	m_hoverNode.reset();
	m_hoverCurve = false;
	update();
	ClipView::leaveEvent(e);
}




float AutomationClipView::pixelsPerTick() const
{
	const float ppb = fixedClips()
		? (parentWidget()->width() - 2 * BORDER_WIDTH) / static_cast<float>(std::max(1, m_clip->length().getBar()))
		: pixelsPerBar();
	return ppb / TimePos::ticksPerBar();
}




float AutomationClipView::xOfTick(int tick) const
{
	return (tick + m_clip->startTimeOffset()) * pixelsPerTick();
}




int AutomationClipView::tickAtX(float x) const
{
	return std::max(0, static_cast<int>(std::lround(x / pixelsPerTick())) - m_clip->startTimeOffset());
}




float AutomationClipView::yOfValue(float value) const
{
	const float min = m_clip->getMin();
	const float max = m_clip->getMax();
	if (max <= min) { return height() / 2.f; }
	return BORDER_WIDTH + (max - value) * (height() - 2 * BORDER_WIDTH) / (max - min);
}




float AutomationClipView::valueAtY(float y) const
{
	const float min = m_clip->getMin();
	const float max = m_clip->getMax();
	const float span = std::max(1.f, static_cast<float>(height() - 2 * BORDER_WIDTH));
	return snapValue(max - (y - BORDER_WIDTH) / span * (max - min));
}




float AutomationClipView::snapValue(float value) const
{
	if (const float step = m_clip->firstObject()->step<float>(); step > 0)
	{
		value = std::round(value / step) * step;
	}
	return std::clamp(value, m_clip->getMin(), m_clip->getMax());
}




bool AutomationClipView::canEditNodes() const
{
	const auto container = m_trackView->trackContainerView();
	return !container->knifeMode() && !container->allowRubberband() && height() >= MinHeightForNodeEditing;
}




std::optional<int> AutomationClipView::nodeAt(const QPoint& pos) const
{
	const auto& timeMap = m_clip->getTimeMap();
	std::optional<int> nearest;
	float nearestDistance = NodeHitRadius * NodeHitRadius + 1;
	for (auto it = timeMap.lowerBound(tickAtX(pos.x() - NodeHitRadius)); it != timeMap.end(); ++it)
	{
		const float dx = xOfTick(POS(it)) - pos.x();
		if (dx > NodeHitRadius) { break; }
		const float dy = yOfValue(INVAL(it)) - pos.y();
		if (const float distance = dx * dx + dy * dy; distance < nearestDistance)
		{
			nearest = POS(it);
			nearestDistance = distance;
		}
	}
	return nearest;
}




bool AutomationClipView::isOnCurve(const QPoint& pos) const
{
	const auto& timeMap = m_clip->getTimeMap();
	// Leave the clip's edges to resizing
	if (timeMap.isEmpty() || pos.x() < resizeGripWidth() || pos.x() >= width() - resizeGripWidth()) { return false; }
	// Nothing is drawn before the first node
	if (tickAtX(pos.x()) < POS(timeMap.begin())) { return false; }

	// Look at a few pixels around the cursor, so steep slopes and jumps are as easy to hit as flat lines
	float top = height();
	float bottom = 0;
	for (int dx = -2; dx <= 2; ++dx)
	{
		const int tick = tickAtX(pos.x() + dx);
		if (tick < POS(timeMap.begin())) { continue; }
		const float y = yOfValue(m_clip->valueAt(TimePos(tick)));
		top = std::min(top, y);
		bottom = std::max(bottom, y);
	}
	return pos.y() >= top - CurveHitDistance && pos.y() <= bottom + CurveHitDistance;
}




void AutomationClipView::updateHover(const QPoint& pos)
{
	std::optional<int> node;
	bool curve = false;
	if (canEditNodes() && rect().contains(pos))
	{
		node = nodeAt(pos);
		curve = !node && isOnCurve(pos);
	}

	if (node != m_hoverNode || curve != m_hoverCurve)
	{
		m_hoverNode = node;
		m_hoverCurve = curve;
		update();
	}

	if (node) { setCursor(Qt::OpenHandCursor); }
	else if (curve) { setCursor(Qt::CrossCursor); }
}




void AutomationClipView::showNodeValue(int tick)
{
	const auto it = m_clip->getTimeMap().constFind(tick);
	if (it == m_clip->getTimeMap().constEnd()) { return; }

	const auto model = m_clip->firstObject();
	const auto pos = TimePos(tick + m_clip->startPosition() + m_clip->startTimeOffset());
	auto bubble = textFloat();
	bubble->setTitle(m_clip->objects().empty() ? tr("Automation point") : model->displayName());
	bubble->setText(tr("%1 at %2:%3").arg(model->displayValue(INVAL(it)))
		.arg(pos.getBar() + 1).arg(pos.getTicks() % TimePos::ticksPerBar()));
	bubble->moveGlobal(this, QPoint(width() + 2, height() + 2));
	bubble->show();
}




void AutomationClipView::paintNodes(QPainter& p)
{
	const auto& timeMap = m_clip->getTimeMap();
	const QColor fill = textColor();
	const QColor outline = QColor(0, 0, 0, 200);

	p.save();
	p.setRenderHint(QPainter::Antialiasing, true);
	float lastX = -NodeRadius * 2;
	for (auto it = timeMap.lowerBound(tickAtX(-NodeRadius)); it != timeMap.end(); ++it)
	{
		const float x = xOfTick(POS(it));
		if (x > width() + NodeRadius) { break; }

		const bool highlighted = POS(it) == m_dragNode || POS(it) == m_hoverNode;
		// Densely packed nodes (like recorded ones) would only draw a thick line
		if (!highlighted && x - lastX < NodeRadius) { continue; }
		lastX = x;

		const float radius = highlighted ? NodeRadius + 1.5f : NodeRadius;
		p.setPen(QPen(outline, 1.0));
		p.setBrush(highlighted ? fill : fill.darker(115));
		p.drawEllipse(QPointF(x, yOfValue(INVAL(it))), radius, radius);
	}
	p.restore();
}




void AutomationClipView::paintEvent( QPaintEvent * )
{
	QPainter painter( this );

	if( !needsUpdate() )
	{
		painter.drawPixmap( 0, 0, m_paintPixmap );
		return;
	}

	setNeedsUpdate( false );

	if (m_paintPixmap.isNull() || m_paintPixmap.size() != size())
	{
		m_paintPixmap = QPixmap(size());
	}

	QPainter p( &m_paintPixmap );

	QLinearGradient lingrad( 0, 0, 0, height() );
	QColor c = getColorForDisplay( painter.background().color() );
	bool muted = m_clip->getTrack()->isMuted() || m_clip->isMuted();
	bool current = getGUI()->automationEditor()->currentClip() == m_clip;

	lingrad.setColorAt( 1, c.darker( 300 ) );
	lingrad.setColorAt( 0, c );

	beginClipPaint(p, m_paintPixmap, rect());

	// paint a black rectangle under the clip to prevent glitches with transparent backgrounds
	p.fillRect( rect(), QColor( 0, 0, 0 ) );

	if( gradient() )
	{
		p.fillRect( rect(), lingrad );
	}
	else
	{
		p.fillRect( rect(), c );
	}

	// pixels per bar
	const float ppb = fixedClips() ?
			( parentWidget()->width() - 2 * BORDER_WIDTH )
				/ (float) m_clip->length().getBar() :
								pixelsPerBar();

	const auto min = m_clip->firstObject()->minValue<float>();
	const auto max = m_clip->firstObject()->maxValue<float>();

	const float y_scale = max - min;
	const float h = ( height() - 2 * BORDER_WIDTH ) / y_scale;
	const float ppTick  = ppb / TimePos::ticksPerBar();
	const int offset =  m_clip->startTimeOffset() * ppTick;

	// Matches yOfValue(): max is at the top border, min at the bottom one
	p.translate(0.0f, BORDER_WIDTH + max * h);
	p.scale( 1.0f, -h );

	QLinearGradient lin2grad( 0, min, 0, max );
	QColor col;

	col = !muted ? painter.pen().brush().color() : mutedColor();

	lin2grad.setColorAt( 1, col.lighter( 150 ) );
	lin2grad.setColorAt( 0.5, col );
	lin2grad.setColorAt( 0, col.darker( 150 ) );

	p.setRenderHints( QPainter::Antialiasing, true );
	for( AutomationClip::timeMap::const_iterator it =
						m_clip->getTimeMap().begin();
					it != m_clip->getTimeMap().end(); ++it )
	{
		const auto nit = std::next(it);

		if (nit == m_clip->getTimeMap().end())
		{
			const float x1 = POS(it) * ppTick + offset;
			const auto x2 = (float)(width() - BORDER_WIDTH);
			if( x1 > ( width() - BORDER_WIDTH ) ) break;
			// We are drawing the space after the last node, so we use the outValue
			if( gradient() )
			{
				p.fillRect(QRectF(x1, 0.0f, x2 - x1, OUTVAL(it)), lin2grad);
			}
			else
			{
				p.fillRect(QRectF(x1, 0.0f, x2 - x1, OUTVAL(it)), col);
			}
			break;
		}

		float *values = m_clip->valuesAfter(POS(it));

		// We are creating a path to draw a polygon representing the values between two
		// nodes. When we have two nodes with discrete progression, we will basically have
		// a rectangle with the outValue of the first node (that's why nextValue will match
		// the outValue of the current node). When we have nodes with linear or cubic progression
		// the value of the end of the shape between the two nodes will be the inValue of
		// the next node.
		float nextValue = m_clip->progressionType() == AutomationClip::ProgressionType::Discrete
			? OUTVAL(it)
			: INVAL(nit);

		QPainterPath path;
		QPointF origin = QPointF(POS(it) * ppTick + offset, 0.0f);
		path.moveTo(origin);
		path.moveTo(QPointF(POS(it) * ppTick + offset, values[0]));
		for (int i = POS(it) + 1; i < POS(nit); ++i)
		{
			float x = i * ppTick + offset;
			if(x > (width() - BORDER_WIDTH)) break;
			float value = values[i - POS(it)];
			path.lineTo(QPointF(x, value));
		}
		path.lineTo((POS(nit)) * ppTick + offset, nextValue);
		path.lineTo((POS(nit)) * ppTick + offset, 0.0f);
		path.lineTo(origin);

		if( gradient() )
		{
			p.fillPath( path, lin2grad );
		}
		else
		{
			p.fillPath( path, col );
		}
		delete [] values;
	}

	p.setRenderHints( QPainter::Antialiasing, false );
	p.resetTransform();

	// bar lines
	const int lineSize = 3;
	p.setPen( c.darker( 300 ) );

	for (bar_t b = 1; b < width() - BORDER_WIDTH; ++b)
	{
		const int bx = BORDER_WIDTH + static_cast<int>(ppb * b) - 2;

		//top line
		p.drawLine(bx + offset, BORDER_WIDTH, bx + offset, BORDER_WIDTH + lineSize);

		//bottom line
		p.drawLine(bx + offset, rect().bottom() - (lineSize + BORDER_WIDTH), bx + offset, rect().bottom() - BORDER_WIDTH);
	}

	// recording icon for when recording automation
	if( m_clip->isRecording() )
	{
		static auto s_clipRec = embed::getIconPixmap("clip_rec");
		p.drawPixmap(1, rect().bottom() - s_clipRec.height(), s_clipRec);
	}

	// clip name
	paintTextLabel(m_clip->name(), p);

	paintClipFrame(p, rect(), c, current);

	// the points can be dragged right here, so show them while the mouse is over the clip
	if ((m_hovered || m_dragNode) && canEditNodes()) { paintNodes(p); }

	// draw the 'muted' pixmap only if the clip was manually muted
	if( m_clip->isMuted() )
	{
		const int spacing = BORDER_WIDTH;
		const int size = 14;
		p.drawPixmap( spacing, height() - ( size + spacing ),
			embed::getIconPixmap( "muted", size, size ) );
	}
	
	if (m_marker)
	{
		p.setPen(markerColor());
		p.drawLine(m_markerPos, rect().bottom(), m_markerPos, rect().top());
	}

	p.end();

	painter.drawPixmap( 0, 0, m_paintPixmap );
}




void AutomationClipView::dragEnterEvent( QDragEnterEvent * _dee )
{
	StringPairDrag::processDragEnterEvent( _dee, "automatable_model" );
	if( !_dee->isAccepted() )
	{
		ClipView::dragEnterEvent( _dee );
	}
}




void AutomationClipView::dropEvent( QDropEvent * _de )
{
	QString type = StringPairDrag::decodeKey( _de );
	QString val = StringPairDrag::decodeValue( _de );
	if( type == "automatable_model" )
	{
		auto mod = dynamic_cast<AutomatableModel*>(Engine::projectJournal()->journallingObject(val.toInt()));
		if( mod != nullptr )
		{
			bool added = m_clip->addObject( mod );
			if ( !added )
			{
				TextFloat::displayMessage( mod->displayName(),
							   tr( "Model is already connected "
							   "to this clip." ),
							   embed::getIconPixmap( "automation" ),
							   2000 );
			}
		}
		update();

		if( getGUI()->automationEditor() &&
			getGUI()->automationEditor()->currentClip() == m_clip )
		{
			getGUI()->automationEditor()->setCurrentClip( m_clip );
		}
	}
	else
	{
		ClipView::dropEvent( _de );
	}
}




void AutomationClipView::scaleTimemapToFit( float oldMin, float oldMax )
{
	float newMin = m_clip->getMin();
	float newMax = m_clip->getMax();

	if( oldMin == newMin && oldMax == newMax )
	{
		return;
	}

	// TODO: Currently when rescaling the timeMap values to fit the new range of values (newMin and newMax)
	// only the inValue is being considered and the outValue is being reset to the inValue (so discrete jumps
	// are discarded). Possibly later we will want discrete jumps to be maintained so we will need to upgrade
	// the logic to account for them.
	for( AutomationClip::timeMap::iterator it = m_clip->m_timeMap.begin();
		it != m_clip->m_timeMap.end(); ++it )
	{
		// If the values are out of the previous range, fix them so they are
		// between oldMin and oldMax.
		if (INVAL(it) < oldMin)
		{
			it.value().setInValue(oldMin);
		}
		else if (INVAL(it) > oldMax)
		{
			it.value().setInValue(oldMax);
		}
		// Calculate what the value would be proportionally in the new range
		it.value().setInValue((INVAL(it) - oldMin) * (newMax - newMin) / (oldMax - oldMin) + newMin);
		// Read earlier TODO comment: For now I'm discarding the discrete jumps during the rescaling
		it.value().setOutValue(INVAL(it));
	}

	m_clip->generateTangents();
}

} // namespace lmms::gui
