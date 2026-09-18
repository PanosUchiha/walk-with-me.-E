#ifndef WAYFARER_MODERN_COMMONLIB
// Collector backport from CommonLibSSE-NG 7.5.1, previously supplied by the
// old handholding translation unit. Native obstacle queries still need it.
namespace RE
{
    hkpRayHitCollector::~hkpRayHitCollector() {}
    hkpClosestRayHitCollector::~hkpClosestRayHitCollector() {}
    void hkpClosestRayHitCollector::AddRayHit(const hkpCdBody& body,
                                              const hkpShapeRayCastCollectorOutput& hit)
    {
        using Function = decltype(&hkpClosestRayHitCollector::AddRayHit);
        static REL::Relocation<Function> function{RELOCATION_ID(59653, 60338)};
        function(this, body, hit);
    }
} // namespace RE
#endif
