#include "dicom_processor/dictionary.hpp"

#include <unordered_map>

namespace dicom
{

    VR lookupImplicitVR(Tag tag)
    {
        static const std::unordered_map<Tag, VR, TagHash> table{
            {tags::TransferSyntaxUID, VR::UI},
            {tags::Modality, VR::CS},
            {tags::StudyDate, VR::DA},
            {tags::PatientID, VR::LO},
            {tags::SliceThickness, VR::DS},
            {tags::ImagePositionPatient, VR::DS},
            {tags::SamplesPerPixel, VR::US},
            {tags::PhotometricInterpretation, VR::CS},
            {tags::Rows, VR::US},
            {tags::Columns, VR::US},
            {tags::PixelSpacing, VR::DS},
            {tags::BitsAllocated, VR::US},
            {tags::BitsStored, VR::US},
            {tags::PixelRepresentation, VR::US},
            {tags::RescaleIntercept, VR::DS},
            {tags::RescaleSlope, VR::DS},
            // PixelData's VR under Implicit VR depends on BitsAllocated (OW for
            // 16-bit, OB for 8-bit) per convention; OW is used as the dictionary
            // default and the parser doesn't rely on this distinction since it
            // reads pixel bytes by length regardless of OB vs OW.
            {tags::PixelData, VR::OW},
        };

        const auto it = table.find(tag);
        return it != table.end() ? it->second : VR::Unknown;
    }

} // namespace dicom