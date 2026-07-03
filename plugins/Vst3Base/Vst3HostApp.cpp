/*
 * Vst3HostApp.cpp - host context objects for the VST3 host
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

#include "Vst3HostApp.h"

#include <cstring>

#include "Vst3Basics.h"

namespace lmms::vst3
{

using namespace Steinberg;

namespace
{

bool iidEqual(const TUID a, const TUID b)
{
	return std::memcmp(a, b, sizeof(TUID)) == 0;
}

} // namespace


/*
	MemoryStream
*/

tresult PLUGIN_API MemoryStream::queryInterface(const TUID iid, void** obj)
{
	if (iidEqual(iid, FUnknown::iid) || iidEqual(iid, IBStream::iid))
	{
		addRef();
		*obj = static_cast<IBStream*>(this);
		return kResultOk;
	}
	*obj = nullptr;
	return kNoInterface;
}




tresult PLUGIN_API MemoryStream::read(void* buffer, int32 numBytes, int32* numBytesRead)
{
	const auto available = static_cast<int64>(m_data.size()) - m_pos;
	const auto toRead = static_cast<int32>(std::max<int64>(0,
		std::min<int64>(numBytes, available)));
	if (toRead > 0)
	{
		std::memcpy(buffer, m_data.constData() + m_pos, toRead);
		m_pos += toRead;
	}
	if (numBytesRead) { *numBytesRead = toRead; }
	return kResultOk;
}




tresult PLUGIN_API MemoryStream::write(void* buffer, int32 numBytes, int32* numBytesWritten)
{
	if (numBytes < 0) { return kInvalidArgument; }
	if (m_pos + numBytes > m_data.size())
	{
		m_data.resize(static_cast<int>(m_pos + numBytes));
	}
	std::memcpy(m_data.data() + m_pos, buffer, numBytes);
	m_pos += numBytes;
	if (numBytesWritten) { *numBytesWritten = numBytes; }
	return kResultOk;
}




tresult PLUGIN_API MemoryStream::seek(int64 pos, int32 mode, int64* result)
{
	int64 newPos = m_pos;
	switch (mode)
	{
		case kIBSeekSet: newPos = pos; break;
		case kIBSeekCur: newPos = m_pos + pos; break;
		case kIBSeekEnd: newPos = m_data.size() + pos; break;
		default: return kInvalidArgument;
	}
	if (newPos < 0) { return kInvalidArgument; }
	m_pos = newPos;
	if (result) { *result = m_pos; }
	return kResultOk;
}




tresult PLUGIN_API MemoryStream::tell(int64* pos)
{
	if (!pos) { return kInvalidArgument; }
	*pos = m_pos;
	return kResultOk;
}




/*
	HostAttributeList
*/

tresult PLUGIN_API HostAttributeList::queryInterface(const TUID iid, void** obj)
{
	if (iidEqual(iid, FUnknown::iid) || iidEqual(iid, Vst::IAttributeList::iid))
	{
		addRef();
		*obj = static_cast<Vst::IAttributeList*>(this);
		return kResultOk;
	}
	*obj = nullptr;
	return kNoInterface;
}




tresult PLUGIN_API HostAttributeList::setInt(AttrID id, int64 value)
{
	if (!id) { return kInvalidArgument; }
	m_values[id] = value;
	return kResultOk;
}




tresult PLUGIN_API HostAttributeList::getInt(AttrID id, int64& value)
{
	if (!id) { return kInvalidArgument; }
	const auto it = m_values.find(id);
	if (it == m_values.end()) { return kResultFalse; }
	if (const auto v = std::get_if<int64>(&it->second)) { value = *v; return kResultOk; }
	return kResultFalse;
}




tresult PLUGIN_API HostAttributeList::setFloat(AttrID id, double value)
{
	if (!id) { return kInvalidArgument; }
	m_values[id] = value;
	return kResultOk;
}




tresult PLUGIN_API HostAttributeList::getFloat(AttrID id, double& value)
{
	if (!id) { return kInvalidArgument; }
	const auto it = m_values.find(id);
	if (it == m_values.end()) { return kResultFalse; }
	if (const auto v = std::get_if<double>(&it->second)) { value = *v; return kResultOk; }
	return kResultFalse;
}




tresult PLUGIN_API HostAttributeList::setString(AttrID id, const Vst::TChar* string)
{
	if (!id || !string) { return kInvalidArgument; }
	m_values[id] = std::u16string(reinterpret_cast<const char16_t*>(string));
	return kResultOk;
}




tresult PLUGIN_API HostAttributeList::getString(AttrID id, Vst::TChar* string, uint32 sizeInBytes)
{
	if (!id || !string) { return kInvalidArgument; }
	const auto it = m_values.find(id);
	if (it == m_values.end()) { return kResultFalse; }
	const auto v = std::get_if<std::u16string>(&it->second);
	if (!v) { return kResultFalse; }

	const uint32 maxChars = sizeInBytes / sizeof(Vst::TChar);
	if (maxChars == 0) { return kResultFalse; }
	const uint32 len = std::min<uint32>(static_cast<uint32>(v->size()), maxChars - 1);
	std::memcpy(string, v->data(), len * sizeof(Vst::TChar));
	string[len] = 0;
	return kResultOk;
}




tresult PLUGIN_API HostAttributeList::setBinary(AttrID id, const void* data, uint32 sizeInBytes)
{
	if (!id || (!data && sizeInBytes > 0)) { return kInvalidArgument; }
	m_values[id] = QByteArray(static_cast<const char*>(data), sizeInBytes);
	return kResultOk;
}




tresult PLUGIN_API HostAttributeList::getBinary(AttrID id, const void*& data, uint32& sizeInBytes)
{
	if (!id) { return kInvalidArgument; }
	const auto it = m_values.find(id);
	if (it == m_values.end()) { return kResultFalse; }
	const auto v = std::get_if<QByteArray>(&it->second);
	if (!v) { return kResultFalse; }
	data = v->constData();
	sizeInBytes = static_cast<uint32>(v->size());
	return kResultOk;
}




/*
	HostMessage
*/

HostMessage::HostMessage() :
	m_attributes(new HostAttributeList())
{
}




HostMessage::~HostMessage()
{
	m_attributes->release();
}




tresult PLUGIN_API HostMessage::queryInterface(const TUID iid, void** obj)
{
	if (iidEqual(iid, FUnknown::iid) || iidEqual(iid, Vst::IMessage::iid))
	{
		addRef();
		*obj = static_cast<Vst::IMessage*>(this);
		return kResultOk;
	}
	*obj = nullptr;
	return kNoInterface;
}




FIDString PLUGIN_API HostMessage::getMessageID()
{
	return m_messageId.c_str();
}




void PLUGIN_API HostMessage::setMessageID(FIDString id)
{
	m_messageId = id ? id : "";
}




Vst::IAttributeList* PLUGIN_API HostMessage::getAttributes()
{
	return m_attributes;
}




/*
	Vst3HostApp
*/

Vst3HostApp* Vst3HostApp::instance()
{
	static Vst3HostApp s_instance;
	return &s_instance;
}




tresult PLUGIN_API Vst3HostApp::queryInterface(const TUID iid, void** obj)
{
	if (iidEqual(iid, FUnknown::iid) || iidEqual(iid, Vst::IHostApplication::iid))
	{
		*obj = static_cast<Vst::IHostApplication*>(this);
		return kResultOk;
	}
	*obj = nullptr;
	return kNoInterface;
}




tresult PLUGIN_API Vst3HostApp::getName(Vst::String128 name)
{
	toVstString(name, QStringLiteral("LMMS"));
	return kResultOk;
}




tresult PLUGIN_API Vst3HostApp::createInstance(TUID cid, TUID iid, void** obj)
{
	if (iidEqual(cid, Vst::IMessage::iid) && iidEqual(iid, Vst::IMessage::iid))
	{
		*obj = static_cast<Vst::IMessage*>(new HostMessage());
		return kResultOk;
	}
	if (iidEqual(cid, Vst::IAttributeList::iid) && iidEqual(iid, Vst::IAttributeList::iid))
	{
		*obj = static_cast<Vst::IAttributeList*>(new HostAttributeList());
		return kResultOk;
	}
	*obj = nullptr;
	return kNoInterface;
}


} // namespace lmms::vst3
