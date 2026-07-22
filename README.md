# DICOM Processor

A multi-threaded C++20 DICOM image processing pipeline: custom binary
ingestion, 3D volume reconstruction with HU normalization, AVX2-accelerated
filtering, region-growing anomaly detection, and PNG/DICOM/JSON export.

**Current status: Week 5 — Foundations & DICOM Ingestion Engine.**
Only the ingestion layer is implemented so far. See
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the full 5-layer design
and the week-by-week build plan.

## Project layout

```
dicom-processor/
├── CMakeLists.txt
├── conanfile.txt
├── docs/
│   └── ARCHITECTURE.md       # full pipeline design (all 5 layers, all weeks)
├── include/dicom_processor/  # public headers for the ingestion layer
│   ├── tag.hpp
│   ├── vr.hpp
│   ├── dictionary.hpp
│   ├── dataset.hpp
│   └── parser.hpp
├── src/                      # ingestion layer implementation + CLI
│   ├── vr.cpp
│   ├── dictionary.cpp
│   ├── dataset.cpp
│   ├── parser.cpp
│   └── main.cpp
├── tools/
│   └── dcmtk_verify.cpp      # optional DCMTK-based cross-check tool
└── data/samples/             # put your test .dcm files here (not committed)
```

## Dependencies

- CMake ≥ 3.20
- A C++20 compiler (GCC ≥ 11, Clang ≥ 14, or MSVC ≥ 19.29)
- [Conan](https://conan.io/) 2.x, for `DCMTK` and `libpng`
  - `DCMTK` is used **only** by the optional `dcmtk_verify` cross-check tool
    (see below) — the core `dicom_processor` binary has no DCMTK dependency.
  - `libpng` is a Week 9 (Output Layer) dependency, pulled in now so it's
    already available when that layer is built.

## Build

```bash
# One-time Conan profile setup, if you don't already have one
conan profile detect --force

# Resolve dependencies and generate the CMake toolchain
conan install . --output-folder=build --build=missing -s build_type=RelWithDebInfo

# Configure and build
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake
cmake --build build
```

This produces two executables in `build/`:

| Binary | Purpose | Depends on DCMTK? |
|---|---|---|
| `dicom_processor` | The actual pipeline — custom parser (this project's real deliverable) | No |
| `dcmtk_verify` | Independent sanity check: loads the same file via DCMTK's reader so you can diff its output against `dicom_processor`'s | Yes |

If you don't have DCMTK set up yet and just want to build the core parser,
skip the verification tool:

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DBUILD_DCMTK_VERIFY_TOOL=OFF
```

## Usage

```bash
./build/dicom_processor data/samples/ct_sample.dcm
```

Prints extracted metadata (Modality, Rows/Columns, BitsAllocated,
RescaleSlope/Intercept) and pixel-data statistics (raw range, HU range for
CT, a small pixel-value patch) for each file given. Multiple files can be
passed at once:

```bash
./build/dicom_processor data/samples/*.dcm
```

Cross-check against DCMTK on the same file:

```bash
./build/dcmtk_verify data/samples/ct_sample.dcm
```

### Supported vs. unsupported input

| Feature | Status |
|---|---|
| Explicit VR Little Endian | ✅ |
| Implicit VR Little Endian | ✅ |
| Explicit VR Big Endian | ✅ |
| 8-bit and 16-bit pixel data | ✅ |
| Compressed pixel data (JPEG / JPEG2000 / RLE transfer syntaxes) | ❌ rejected with a clear `ParseError`, by design — see `ARCHITECTURE.md` §11 |
| Nested DICOM sequences (SQ) | Cursor advances past them correctly; sequence-internal field extraction not yet implemented (not needed for any tag this project currently reads) |

## Testing the parser without real sample data

`tools/` doesn't currently include a synthetic-file generator, but the
parser was validated during development against hand-built minimal DICOM
files covering all three supported transfer syntaxes. If you need to
regenerate similar fixtures, construct a file with:

1. A 128-byte preamble + `"DICM"` magic
2. A File Meta Info group `(0002,xxxx)` (always Explicit VR Little Endian)
   containing at least `TransferSyntaxUID (0002,0010)`
3. A main dataset encoded per that transfer syntax, including `Modality
   (0008,0060)`, `Rows (0028,0010)`, `Columns (0028,0011)`, `BitsAllocated
   (0028,0100)`, and `PixelData (7FE0,0010)`

## Roadmap

See the traceability table in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md#12-week-by-week-traceability)
for what each remaining week (Reconstruction, Processing, Detection,
Output) delivers.