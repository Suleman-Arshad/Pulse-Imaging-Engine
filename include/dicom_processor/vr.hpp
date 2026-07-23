#pragma once
#include <cstdint>
#include <string_view>

namespace dicom
{

    enum class VR : uint8_t
    {
        AE,
        AS,
        AT,
        CS,
        DA,
        DS,
        DT,
        FL,
        FD,
        IS,
        LO,
        LT,
        OB,
        OD,
        OF,
        OL,
        OW,
        PN,
        SH,
        SL,
        SQ,
        SS,
        ST,
        TM,
        UC,
        UI,
        UL,
        UN,
        UR,
        US,
        UT,
        Unknown // implicit-VR tag not in our dictionary, or malformed explicit code
    };

    // Parses the 2-character ASCII VR code found in Explicit VR elements.
    VR vrFromString(std::string_view code);

    // For diagnostics/printing.
    std::string_view vrToString(VR vr);

    // True for the VRs that use the "2 reserved bytes + 4-byte length" encoding under Explicit VR . All other Explicit VR elements use a plain 2-byte length. Implicit VR always uses a 4-byte length regardless of this function.
    bool usesLongLengthForm(VR vr);

} // namespace dicom