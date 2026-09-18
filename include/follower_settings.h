#pragma once
#include <SimpleIni.h>

namespace Wayfarer
{
    inline bool MigrateFollowerSettings(CSimpleIniA& ini)
    {
        if (ini.GetLongValue("General", "iEnrollmentVersion", 0) >= 2) {
            return false;
        }
        ini.SetBoolValue("General", "bAutoDiscover", false);
        ini.SetLongValue("General", "iEnrollmentVersion", 2);
        return true;
    }
} // namespace Wayfarer
