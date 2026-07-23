#pragma once
#include "dicom_processor/dataset.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace dicom
{

    enum class TransferSyntax
    {
        ImplicitVRLittleEndian, // 1.2.840.10008.1.2
        ExplicitVRLittleEndian, // 1.2.840.10008.1.2.1
        ExplicitVRBigEndian,    // 1.2.840.10008.1.2.2
    };

    // Thrown for malformed files and for transfer syntaxes this parser doesn't
    // support (compressed pixel data: JPEG/JPEG2000/RLE — see ARCHITECTURE.md
    // section 11). Never thrown for "value decodes to something unexpected";
    // only for structural problems the parser can't safely proceed past.
    struct ParseError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    // Result of parsing one DICOM file: its metadata plus decoded pixel data,
    // widened to uint16_t regardless of source bit depth so downstream layers
    // (reconstruction, filtering) have one consistent pixel type to work with.
    struct Slice
    {
        Dataset metadata;
        std::vector<uint16_t> pixels; // row-major, size == rows * columns

        std::string modality;
        int rows{0};
        int columns{0};
        int bitsAllocated{0};
        double rescaleSlope{1.0};
        double rescaleIntercept{0.0};

        // Converts a raw stored pixel value to Hounsfield Units (CT) using this
        // slice's rescale parameters. For non-CT modalities the rescale
        // defaults (slope=1, intercept=0) make this a no-op passthrough.
        double huAt(int x, int y) const
        {
            const size_t index = static_cast<size_t>(y) * static_cast<size_t>(columns) + static_cast<size_t>(x);
            return static_cast<double>(pixels.at(index)) * rescaleSlope + rescaleIntercept;
        }
    };

    class Parser
    {
    public:
        // Reads and fully parses one .dcm file. Throws ParseError on any
        // structural problem (bad magic, truncated stream, unsupported
        // transfer syntax, pixel data length mismatch).
        static Slice parseFile(const std::string &path);
    };

} // namespace dicom