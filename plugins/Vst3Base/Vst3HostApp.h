/*
 * Vst3HostApp.h - host context objects for the VST3 host
 *
 * Copyright (c) 2026 LMMS developers
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

#ifndef LMMS_VST3_HOST_APP_H
#define LMMS_VST3_HOST_APP_H

#include <QByteArray>
#include <atomic>
#include <map>
#include <string>
#include <variant>

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include "vst3base_export.h"

namespace lmms::vst3
{

//! Simple in-memory IBStream used for component/controller state
class VST3BASE_EXPORT MemoryStream : public Steinberg::IBStream
{
public:
	MemoryStream() = default;
	explicit MemoryStream(const QByteArray& data) : m_data(data) {}
	virtual ~MemoryStream() = default;

	// FUnknown
	Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override;
	Steinberg::uint32 PLUGIN_API addRef() override { return ++m_refCount; }
	Steinberg::uint32 PLUGIN_API release() override
	{
		const auto r = --m_refCount;
		if (r == 0) { delete this; return 0; }
		return r;
	}

	// IBStream
	Steinberg::tresult PLUGIN_API read(void* buffer, Steinberg::int32 numBytes,
		Steinberg::int32* numBytesRead) override;
	Steinberg::tresult PLUGIN_API write(void* buffer, Steinberg::int32 numBytes,
		Steinberg::int32* numBytesWritten) override;
	Steinberg::tresult PLUGIN_API seek(Steinberg::int64 pos, Steinberg::int32 mode,
		Steinberg::int64* result) override;
	Steinberg::tresult PLUGIN_API tell(Steinberg::int64* pos) override;

	const QByteArray& data() const { return m_data; }
	void rewind() { m_pos = 0; }

private:
	QByteArray m_data;
	Steinberg::int64 m_pos = 0;
	std::atomic<Steinberg::int32> m_refCount{1};
};


//! IAttributeList implementation handed to plugins via IMessage
class HostAttributeList : public Steinberg::Vst::IAttributeList
{
public:
	virtual ~HostAttributeList() = default;

	// FUnknown
	Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override;
	Steinberg::uint32 PLUGIN_API addRef() override { return ++m_refCount; }
	Steinberg::uint32 PLUGIN_API release() override
	{
		const auto r = --m_refCount;
		if (r == 0) { delete this; return 0; }
		return r;
	}

	// IAttributeList
	Steinberg::tresult PLUGIN_API setInt(AttrID id, Steinberg::int64 value) override;
	Steinberg::tresult PLUGIN_API getInt(AttrID id, Steinberg::int64& value) override;
	Steinberg::tresult PLUGIN_API setFloat(AttrID id, double value) override;
	Steinberg::tresult PLUGIN_API getFloat(AttrID id, double& value) override;
	Steinberg::tresult PLUGIN_API setString(AttrID id, const Steinberg::Vst::TChar* string) override;
	Steinberg::tresult PLUGIN_API getString(AttrID id, Steinberg::Vst::TChar* string,
		Steinberg::uint32 sizeInBytes) override;
	Steinberg::tresult PLUGIN_API setBinary(AttrID id, const void* data,
		Steinberg::uint32 sizeInBytes) override;
	Steinberg::tresult PLUGIN_API getBinary(AttrID id, const void*& data,
		Steinberg::uint32& sizeInBytes) override;

private:
	using Value = std::variant<Steinberg::int64, double, std::u16string, QByteArray>;
	std::map<std::string, Value> m_values;
	std::atomic<Steinberg::int32> m_refCount{1};
};


//! IMessage implementation for plugin-internal communication
class HostMessage : public Steinberg::Vst::IMessage
{
public:
	HostMessage();
	virtual ~HostMessage();

	// FUnknown
	Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override;
	Steinberg::uint32 PLUGIN_API addRef() override { return ++m_refCount; }
	Steinberg::uint32 PLUGIN_API release() override
	{
		const auto r = --m_refCount;
		if (r == 0) { delete this; return 0; }
		return r;
	}

	// IMessage
	Steinberg::FIDString PLUGIN_API getMessageID() override;
	void PLUGIN_API setMessageID(Steinberg::FIDString id) override;
	Steinberg::Vst::IAttributeList* PLUGIN_API getAttributes() override;

private:
	std::string m_messageId;
	HostAttributeList* m_attributes;
	std::atomic<Steinberg::int32> m_refCount{1};
};


//! The host context (IHostApplication) passed to components and controllers
class VST3BASE_EXPORT Vst3HostApp : public Steinberg::Vst::IHostApplication
{
public:
	//! Global instance, never deleted
	static Vst3HostApp* instance();

	// FUnknown
	Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override;
	Steinberg::uint32 PLUGIN_API addRef() override { return 1000; }
	Steinberg::uint32 PLUGIN_API release() override { return 1000; }

	// IHostApplication
	Steinberg::tresult PLUGIN_API getName(Steinberg::Vst::String128 name) override;
	Steinberg::tresult PLUGIN_API createInstance(Steinberg::TUID cid, Steinberg::TUID iid,
		void** obj) override;

private:
	Vst3HostApp() = default;
};


} // namespace lmms::vst3

#endif // LMMS_VST3_HOST_APP_H
