#pragma once
#include "dicom_processor/tag.hpp"
#include "dicom_processor/vr.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dicom
{

    struct Element
    {
        Tag tag;
        VR vr{VR::Unknown};
        std::vector<std::byte> value; // raw bytes exactly as they appear on disk
    };

    class Dataset
    {
    public:
        void insert(Element element);

        // Returns nullptr if the tag was not present in the parsed file.
        const Element *find(Tag tag) const;

        // Text VRs (LO, CS, DA, UI, ...): trims DICOM's trailing padding
        // (space for most VRs, null for UI) before returning.
        std::optional<std::string> getString(Tag tag) const;

        // US (unsigned short, 2 bytes). `bigEndian` must match the transfer
        // syntax the value was parsed under.
        std::optional<uint16_t> getUInt16(Tag tag, bool bigEndian) const;

        // DS/IS (decimal/integer string): DICOM stores numbers as ASCII text,
        // optionally backslash-separated for multi-valued fields. This returns
        // only the first value, which is sufficient for the scalar fields this
        // project reads (RescaleSlope, RescaleIntercept, SliceThickness).
        std::optional<double> getDouble(Tag tag) const;

        const std::vector<std::byte> *getRawBytes(Tag tag) const;

        size_t size() const { return elements_.size(); }

    private:
        std::unordered_map<Tag, Element, TagHash> elements_;
    };

} // namespace dicom