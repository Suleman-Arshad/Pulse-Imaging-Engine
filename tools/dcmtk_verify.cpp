#include <dcmtk/config/osconfig.h>
#include <dcmtk/dcmdata/dctk.h>

#include <iostream>
#include <string>

namespace
{

    bool tryGetString(DcmDataset &dataset, const DcmTagKey &tag, std::string &out)
    {
        OFString value;
        if (dataset.findAndGetOFString(tag, value).good())
        {
            out = value.c_str();
            return true;
        }
        return false;
    }

} // namespace

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <path-to-dicom-file>\n";
        std::cerr << "  Verifies DCMTK linkage by loading a DICOM file and\n";
        std::cerr << "  printing a handful of core metadata tags.\n";
        return EXIT_FAILURE;
    }

    const std::string filePath = argv[1];

    DcmFileFormat fileFormat;
    const OFCondition status = fileFormat.loadFile(filePath.c_str());

    if (!status.good())
    {
        std::cerr << "Error: could not load '" << filePath << "': "
                  << status.text() << '\n';
        return EXIT_FAILURE;
    }

    DcmDataset *dataset = fileFormat.getDataset();
    if (dataset == nullptr)
    {
        std::cerr << "Error: file loaded but contains no dataset.\n";
        return EXIT_FAILURE;
    }

    std::cout << "=== DCMTK Link Verification: SUCCESS ===\n";
    std::cout << "File: " << filePath << "\n\n";

    // A handful of tags that exist across CT/MRI/X-Ray so this one verification binary works against any of your Week 5 sample files.
    std::string value;

    if (tryGetString(*dataset, DCM_Modality, value))
        std::cout << "Modality:            " << value << '\n';

    if (tryGetString(*dataset, DCM_PatientID, value))
        std::cout << "Patient ID:          " << value << '\n';

    if (tryGetString(*dataset, DCM_StudyDate, value))
        std::cout << "Study Date:          " << value << '\n';

    if (tryGetString(*dataset, DCM_Rows, value))
        std::cout << "Rows:                " << value << '\n';

    if (tryGetString(*dataset, DCM_Columns, value))
        std::cout << "Columns:             " << value << '\n';

    if (tryGetString(*dataset, DCM_BitsAllocated, value))
        std::cout << "Bits Allocated:      " << value << '\n';

    if (tryGetString(*dataset, DCM_PhotometricInterpretation, value))
        std::cout << "Photometric Interp:  " << value << '\n';

    // Confirm pixel data is present and readable — this is the tag your custom parser will need to locate manually in Step 3.
    const DcmElement *pixelElement = nullptr;
    if (dataset->findAndGetElement(DCM_PixelData, const_cast<DcmElement *&>(pixelElement)).good() && pixelElement != nullptr)
    {
        std::cout << "Pixel Data:          present ("
                  << pixelElement->getLength() << " bytes)\n";
    }
    else
    {
        std::cout << "Pixel Data:          NOT FOUND\n";
    }

    std::cout << "\nDCMTK toolchain is correctly linked and readable.\n";
    return EXIT_SUCCESS;
}