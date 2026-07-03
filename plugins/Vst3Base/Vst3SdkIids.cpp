/*
 * Vst3SdkIids.cpp - definitions of the interface ids used by the VST3 host
 *
 * The VST3 pluginterfaces headers only declare the interface ids; exactly
 * one translation unit has to define them (the SDK does this in
 * public.sdk/source/vst/vstinitiids.cpp, which we do not vendor).
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

#include "pluginterfaces/base/funknown.h"

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstunits.h"

namespace Steinberg
{

DEF_CLASS_IID(IPlugView)
DEF_CLASS_IID(IPlugFrame)
DEF_CLASS_IID(IPlugViewContentScaleSupport)

DEF_CLASS_IID(Linux::IEventHandler)
DEF_CLASS_IID(Linux::ITimerHandler)
DEF_CLASS_IID(Linux::IRunLoop)

DEF_CLASS_IID(Vst::IAttributeList)
DEF_CLASS_IID(Vst::IStreamAttributes)
DEF_CLASS_IID(Vst::IAudioProcessor)
DEF_CLASS_IID(Vst::IAudioPresentationLatency)
DEF_CLASS_IID(Vst::IProcessContextRequirements)
DEF_CLASS_IID(Vst::IComponent)
DEF_CLASS_IID(Vst::IEditController)
DEF_CLASS_IID(Vst::IEditController2)
DEF_CLASS_IID(Vst::IComponentHandler)
DEF_CLASS_IID(Vst::IComponentHandler2)
DEF_CLASS_IID(Vst::IEventList)
DEF_CLASS_IID(Vst::IHostApplication)
DEF_CLASS_IID(Vst::IVst3ToVst2Wrapper)
DEF_CLASS_IID(Vst::IVst3ToAUWrapper)
DEF_CLASS_IID(Vst::IConnectionPoint)
DEF_CLASS_IID(Vst::IMessage)
DEF_CLASS_IID(Vst::IMidiMapping)
DEF_CLASS_IID(Vst::IParamValueQueue)
DEF_CLASS_IID(Vst::IParameterChanges)
DEF_CLASS_IID(Vst::IUnitHandler)
DEF_CLASS_IID(Vst::IUnitInfo)
DEF_CLASS_IID(Vst::IProgramListData)
DEF_CLASS_IID(Vst::IUnitData)

} // namespace Steinberg
