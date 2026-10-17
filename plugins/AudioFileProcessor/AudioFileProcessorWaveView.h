/*
 * AudioFileProcessorWaveView.h - Wave renderer of the AFP
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


#ifndef LMMS_AUDIO_FILE_PROCESSOR_WAVE_VIEW_H
#define LMMS_AUDIO_FILE_PROCESSOR_WAVE_VIEW_H

#include <QPixmap>
#include <QWidget>
#include <memory>
#include <vector>

class QTimer;

namespace lmms
{

class AudioFileProcessor;
class SampleBuffer;

namespace gui
{

//! The sample editor: a zoomable waveform with draggable start, loop and end points, fade
//! handles and, in slice mode, slice markers with clickable pads
class AudioFileProcessorWaveView : public QWidget
{
	Q_OBJECT
public:
	AudioFileProcessorWaveView(QWidget* parent, AudioFileProcessor* afp);

	//! Show the whole sample
	void zoomToFit();

signals:
	//! The empty display was clicked
	void openFileRequested();

public slots:
	void sampleUpdated();

protected:
	void paintEvent(QPaintEvent*) override;
	void resizeEvent(QResizeEvent*) override;
	void mousePressEvent(QMouseEvent*) override;
	void mouseMoveEvent(QMouseEvent*) override;
	void mouseReleaseEvent(QMouseEvent*) override;
	void mouseDoubleClickEvent(QMouseEvent*) override;
	void wheelEvent(QWheelEvent*) override;
	void leaveEvent(QEvent*) override;
	void contextMenuEvent(QContextMenuEvent*) override;

private:
	enum class Target { None, Start, End, Loop, FadeIn, FadeOut, Marker, Pad, Overview, View };

	struct Hit
	{
		Target target = Target::None;
		int index = -1; //!< marker or pad index
	};

	struct Peak
	{
		float min = 0.f;
		float max = 0.f;
	};

	// Geometry
	QRect padLane() const;
	QRect waveRect() const;
	QRect rulerRect() const;
	QRect overviewRect() const;

	int sampleSize() const;
	double frameToX(double frame) const;
	double xToFrame(double x) const;
	double fadeFrames(float ms) const;

	Hit hitTest(const QPoint& pos) const;
	void updateCursor(const Hit& hit);
	void dragTo(const QPoint& pos, bool fine);
	int snapped(double frame, bool bypass) const;
	void setView(double from, double to);
	void zoomAt(double x, double factor);

	//! Min/max of logical frames [from, to) using the block cache
	Peak peakOf(double from, double to) const;
	void rebuildPeaks();
	void renderWave();
	QString hoverText() const;
	QString formatTime(double seconds, double step) const;

	void auditionSlice(int index);
	void stopAudition();

	AudioFileProcessor* m_afp;
	std::shared_ptr<const SampleBuffer> m_peaksBuffer;
	std::vector<Peak> m_peaks; //!< per block of BlockSize frames, in buffer order
	static constexpr int BlockSize = 256;

	double m_viewFrom = 0.0;
	double m_viewTo = 1.0;
	bool m_reversed = false;

	QPixmap m_wave;
	bool m_waveDirty = true;

	Hit m_hover;
	Hit m_drag;
	QPoint m_pressPos;
	QPoint m_lastPos;
	double m_dragFrameOffset = 0.0;
	int m_auditionKey = -1;

	QTimer* m_timer;
	unsigned m_lastPlayTick = 0;
	int m_playFrame = -1;
	int m_idleTicks = 0;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_AUDIO_FILE_PROCESSOR_WAVE_VIEW_H
