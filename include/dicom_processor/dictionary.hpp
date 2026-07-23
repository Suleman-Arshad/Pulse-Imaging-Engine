#pragma once
/* dictionary.hpp — Implicit VR Little Endian carries no VR code in the
stream, so the parser must already know each standard tag's VR from the
DICOM Data Dictionary (Part 6). This is a minimal subset covering the
tags this project reads; extend as needed rather than trying to embed
the full ~4000-entry standard dictionary.*/

#include "dicom_processor/tag.hpp"
#include "dicom_processor/vr.hpp"

namespace dicom
{

    // Returns VR::Unknown if the tag isn't in our subset dictionary. Callers should fall back to treating the value as raw/opaque bytes (VR::UN semantics) rather than failing the whole parse.
    VR lookupImplicitVR(Tag tag);   

} // namespace dicom