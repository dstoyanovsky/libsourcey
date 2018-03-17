///
//
// LibSourcey
// Copyright (c) 2005, Sourcey <https://sourcey.com>
//
// SPDX-License-Identifier: LGPL-2.1+
//
/// @addtogroup av
/// @{


#ifndef SCY_AV_Device_H
#define SCY_AV_Device_H

#include "scy/av/av.h"

namespace scy {
namespace av {


/// Represents a system audio, video or render device.
// Extracted to separate *.h file to resolve issue with objectve-c code build.
struct AV_API Device
{
    enum Type
    {
        Unknown,
        VideoInput,
        VideoOutput,
        AudioInput,
        AudioOutput
    };

    Type type;
    std::string id;
    std::string name;
    bool isDefault;

    Device();
    Device(Type type, const std::string& id, const std::string& name, bool isDefault = false);

    void print(std::ostream& os);

    bool operator==(const Device& that) const
    {
        return type == that.type && id == that.id && name == that.name;
    }
};


} // namespace av
} // namespace scy


#endif // SCY_AV_DeviceManager_H
