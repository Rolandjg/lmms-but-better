/*
 * embed.cpp - misc stuff for using embedded resources (linked into binary)
 *
 * Copyright (c) 2004-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "embed.h"

#include <algorithm>

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIconEngine>
#include <QImageReader>
#include <QPainter>
#include <QPixmapCache>
#include <QResource>
#include <QScreen>
#include <QStyle>
#include <QStyleOption>
#include <QSvgRenderer>

namespace lmms::embed {

namespace {

// Render an SVG at the given logical size (or its default size if empty) and device pixel ratio
auto renderSvg(const QString& resourceName, QSize logicalSize, qreal devicePixelRatio) -> QImage
{
	// QFile requires the file extension to be present, unlike QImageReader
	QString fileName = resourceName;
	if (!fileName.endsWith(".svg", Qt::CaseInsensitive)) { fileName += ".svg"; }

	QFile file(fileName);
	if (!file.open(QIODevice::ReadOnly))
	{
		qWarning() << "Failed to open resource for SVG: " << resourceName;
		return {};
	}

	QSvgRenderer renderer(file.readAll());
	if (!renderer.isValid())
	{
		qWarning() << "Error loading SVG file: " << resourceName;
		return {};
	}

	if (logicalSize.isEmpty()) { logicalSize = renderer.defaultSize(); }
	const auto physicalSize = QSize{
		std::max(1, qRound(logicalSize.width() * devicePixelRatio)),
		std::max(1, qRound(logicalSize.height() * devicePixelRatio))
	};

	QImage image(physicalSize, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	QPainter painter(&image);
	renderer.render(&painter);
	painter.end();
	return image;
}

// QPixmapCache and HiDPI compatible SVG-->QPixmap wrapper
auto loadSvgPixmap(const QString& resourceName, int width, int height) -> QPixmap
{
	const auto logicalSize = (width > 0 && height > 0) ? QSize{width, height} : QSize{};
	const qreal devicePixelRatio = QGuiApplication::primaryScreen()->devicePixelRatio();

	const auto image = renderSvg(resourceName, logicalSize, devicePixelRatio);
	if (image.isNull()) { return QPixmap{1, 1}; }

	auto pixmap = QPixmap::fromImage(image);
	pixmap.setDevicePixelRatio(devicePixelRatio);
	return pixmap;
}

// Find the artwork file for a name like "add", preferring SVG over PNG within each artwork directory.
// Qt's own lookup tries every directory for each suffix in turn, so a PNG anywhere would always beat an SVG.
auto resolveArtwork(const QString& name) -> QString
{
	if (QDir::isAbsolutePath(name)) { return name; }

	for (const auto& path : QDir::searchPaths("artwork"))
	{
		const auto base = QDir{path}.filePath(name);
		for (const auto& candidate : {base, base + ".svg", base + ".png"})
		{
			if (QFileInfo{candidate}.isFile()) { return candidate; }
		}
	}

	// Let Qt try any other image formats
	return "artwork:" + name;
}

auto loadPixmap(const QString& name, int width, int height, const char* const* xpm) -> QPixmap
{
	if (xpm) { return QPixmap{xpm}; }

	const auto resourceName = resolveArtwork(name);
	auto reader = QImageReader{resourceName};

	if (reader.format().toLower() == "svg")
	{
		return loadSvgPixmap(resourceName, width, height);
	}

	if (width > 0 && height > 0) { reader.setScaledSize(QSize{width, height}); }

	const auto pixmap = QPixmap::fromImageReader(&reader);
	if (pixmap.isNull()) {
		qWarning().nospace() << "Error loading icon pixmap " << name << ": " << reader.errorString();
		return QPixmap{1, 1};
	}
	return pixmap;
}

//! Icon engine that renders theme artwork at exactly the device pixel size Qt asks for.
//! A QIcon made from a HiDPI QPixmap gets resampled by Qt whenever the pixmap is not exactly the requested size,
//! which blurs SVG artwork. This engine instead renders the artwork at its natural size (scaled down to fit if
//! needed, never up) and centers it in a transparent pixmap of the requested size.
class ArtworkIconEngine : public QIconEngine
{
public:
	ArtworkIconEngine(QString name, const char* const* xpm) :
		m_name{std::move(name)},
		m_xpm{xpm}
	{ }

	auto pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) -> QPixmap override
	{
		return renderIcon(size, mode, 1.0);
	}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	auto scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) -> QPixmap override
	{
		return renderIcon(size, mode, scale);
	}
#endif

	void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
	{
		const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
		const auto physicalSize = QSize{qRound(rect.width() * dpr), qRound(rect.height() * dpr)};
		painter->drawPixmap(rect, renderIcon(physicalSize, mode, dpr));
	}

	auto actualSize(const QSize& size, QIcon::Mode mode, QIcon::State state) -> QSize override
	{
		// The rendered pixmap always fills the requested size (the artwork is centered within it), which keeps
		// QIcon from rescaling it to compensate for a size mismatch
		return size;
	}

	auto clone() const -> QIconEngine* override { return new ArtworkIconEngine{*this}; }

	auto key() const -> QString override { return QStringLiteral("lmms-artwork"); }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	auto availableSizes(QIcon::Mode mode, QIcon::State state) -> QList<QSize> override { return {naturalSize()}; }

	auto isNull() -> bool override { return naturalSize().isEmpty(); }
#else
	void virtual_hook(int id, void* data) override
	{
		switch (id)
		{
		case QIconEngine::ScaledPixmapHook:
		{
			auto& arg = *static_cast<QIconEngine::ScaledPixmapArgument*>(data);
			arg.pixmap = renderIcon(arg.size, arg.mode, arg.scale);
			return;
		}
		case QIconEngine::AvailableSizesHook:
		{
			auto& arg = *static_cast<QIconEngine::AvailableSizesArgument*>(data);
			arg.sizes = QList<QSize>{naturalSize()};
			return;
		}
		case QIconEngine::IsNullHook:
			*static_cast<bool*>(data) = naturalSize().isEmpty();
			return;
		default:
			QIconEngine::virtual_hook(id, data);
		}
	}
#endif

private:
	//! Logical size of the artwork as getIconPixmap() would return it
	auto naturalSize() const -> QSize
	{
		if (!m_naturalSize.isValid())
		{
			const auto pixmap = getIconPixmap(m_name.toStdString(), -1, -1, m_xpm);
			m_naturalSize = logicalSize(pixmap);
		}
		return m_naturalSize;
	}

	//! Render the artwork at the given logical size and device pixel ratio, returning a physical-size image
	auto renderArtwork(QSize logical, qreal dpr) const -> QImage
	{
		const auto physical = QSize{std::max(1, qRound(logical.width() * dpr)), std::max(1, qRound(logical.height() * dpr))};

		if (!m_xpm)
		{
			const auto resourceName = resolveArtwork(m_name);
			if (QImageReader{resourceName}.format().toLower() == "svg")
			{
				return renderSvg(resourceName, logical, dpr);
			}
		}

		// Raster artwork: integer upscales stay pixel-crisp, anything else is smoothed
		auto image = getIconPixmap(m_name.toStdString(), -1, -1, m_xpm).toImage();
		if (image.size() == physical) { return image; }
		const bool integerUpscale = physical.width() % image.width() == 0 && physical.height() % image.height() == 0
			&& physical.width() / image.width() == physical.height() / image.height();
		return image.scaled(physical, Qt::IgnoreAspectRatio,
			integerUpscale ? Qt::FastTransformation : Qt::SmoothTransformation);
	}

	auto renderIcon(const QSize& physicalSize, QIcon::Mode mode, qreal dpr) const -> QPixmap
	{
		if (physicalSize.isEmpty() || dpr <= 0) { return {}; }

		const auto natural = naturalSize();
		if (natural.isEmpty()) { return {}; }

		const auto cacheKey = QStringLiteral("lmms-icon:%1:%2x%3@%4:%5")
			.arg(m_name).arg(physicalSize.width()).arg(physicalSize.height()).arg(dpr).arg(static_cast<int>(mode));
		if (auto cached = QPixmap{}; QPixmapCache::find(cacheKey, &cached)) { return cached; }

		// Keep the natural size, only scaling down when the artwork does not fit
		const auto slot = QSizeF{physicalSize} / dpr;
		auto logical = natural;
		if (logical.width() > slot.width() || logical.height() > slot.height())
		{
			logical = QSizeF{natural}.scaled(slot, Qt::KeepAspectRatio).toSize().expandedTo(QSize{1, 1});
		}

		const auto artwork = renderArtwork(logical, dpr);

		auto canvas = QImage{physicalSize, QImage::Format_ARGB32_Premultiplied};
		canvas.fill(Qt::transparent);
		{
			QPainter painter(&canvas);
			painter.drawImage((physicalSize.width() - artwork.width()) / 2,
				(physicalSize.height() - artwork.height()) / 2, artwork);
		}

		auto pixmap = QPixmap::fromImage(canvas);
		if (mode == QIcon::Disabled || mode == QIcon::Selected)
		{
			auto option = QStyleOption{};
			option.palette = QGuiApplication::palette();
			pixmap = QApplication::style()->generatedIconPixmap(mode, pixmap, &option);
		}
		pixmap.setDevicePixelRatio(dpr);

		QPixmapCache::insert(cacheKey, pixmap);
		return pixmap;
	}

	QString m_name;
	const char* const* m_xpm = nullptr;
	mutable QSize m_naturalSize;
};

} // namespace

auto getIconPixmap(std::string_view name, int width, int height, const char* const* xpm) -> QPixmap
{
	if (name.empty()) { return QPixmap{}; }

	const auto pixmapName = QString::fromUtf8(name.data(), name.size());
	const auto cacheName = (width > 0 && height > 0)
		? QStringLiteral("%1_%2_%3").arg(pixmapName).arg(width).arg(height)
		: pixmapName;

	// Return cached pixmap if it exists
	if (auto pixmap = QPixmap{}; QPixmapCache::find(cacheName, &pixmap)) { return pixmap; }

	// Load the pixmap and cache it before returning
	const auto pixmap = loadPixmap(pixmapName, width, height, xpm);
	QPixmapCache::insert(cacheName, pixmap);
	return pixmap;
}

auto getIcon(std::string_view name, const char* const* xpm) -> QIcon
{
	if (name.empty() && !xpm) { return QIcon{}; }
	return QIcon{new ArtworkIconEngine{QString::fromUtf8(name.data(), name.size()), xpm}};
}

auto getText(std::string_view name) -> QString
{
	const auto resource = QResource{":/" + QString::fromUtf8(name.data(), name.size())};
	if (!resource.isValid())
	{
		qWarning().nospace() << "Error loading text resource '"
			<< QString::fromUtf8(name.data(), name.size()) << "'";
		return {};
	}
	const auto data = resource.uncompressedData();
	if (data.isNull())
	{
		qWarning().nospace() << "Error reading text resource '"
			<< QString::fromUtf8(name.data(), name.size()) << "'";
		return {};
	}
	return QString::fromUtf8(data);
}

} // namespace lmms::embed
