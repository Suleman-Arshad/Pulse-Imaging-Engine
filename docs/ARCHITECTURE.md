# Architecture Design Document
## High-Performance Medical Imaging Pipeline (DICOM Processor)

**Version:** 0.1 (Week 5 baseline)
**Status:** Living document — updated at the end of each week as layers are implemented.

---

## 1. Overview & Goals

This project is a multi-threaded C++20 pipeline that ingests raw DICOM files (CT,
MRI, X-Ray), reconstructs them into a 3D voxel volume, applies SIMD-accelerated
filters, detects density anomalies via region growing, and exports annotated
results.

**Design goals, in priority order:**

1. **Correctness first.** Medical pixel data is meaningless if the parser
   mishandles endianness, VR, or HU rescaling — silent corruption is worse
   than a crash.
2. **Throughput.** Target: process a 512×512×300-slice CT series (~150M
   voxels) through the full pipeline (ingest → reconstruct → filter →
   detect → export) in well under a minute on a modern 8-core desktop.
3. **Memory discipline.** A single full-body CT volume at 16-bit depth is
   ~300 MB raw; the pipeline should avoid gratuitous copies (prefer views/
   spans over new allocations between layers).
4. **Extensibility.** Each layer is a swappable unit — e.g., the detection
   layer's region-growing algorithm should be replaceable later without
   touching ingestion or reconstruction code.

**Why C++20 + AVX2 instead of Python/NumPy/SimpleITK:** this is explicitly an
internship exercise in systems-level performance engineering — hand-rolled
parsing, manual SIMD, and custom thread pooling are the point, not just the
means to an end. Production DICOM work in industry would typically lean on
DCMTK/ITK far more heavily than this project does by design.

---

## 2. System-Level Data Flow

```
 ┌─────────────────┐
 │  Raw .dcm files │  (CT / MRI / X-Ray, one file per slice)
 └────────┬────────┘
          │  bytes
          ▼
 ┌─────────────────────────────┐
 │  1. INGESTION LAYER         │
 │  - Tag/VR/endianness parser │
 │  - Metadata extraction      │
 │  - Pixel data extraction    │
 └────────┬────────────────────┘
          │  DicomSlice { metadata, pixel buffer }
          ▼
 ┌─────────────────────────────┐
 │  2. RECONSTRUCTION LAYER    │
 │  - Slice ordering/validation│
 │  - HU normalization         │
 │  - Trilinear interpolation  │
 │    (anisotropic → isotropic)│
 └────────┬────────────────────┘
          │  VoxelVolume (dense 3D float grid)
          ▼
 ┌─────────────────────────────┐
 │  3. PROCESSING LAYER        │
 │  - Thread pool (task queue) │
 │  - AVX2 Gaussian blur       │
 │  - AVX2 edge detection      │
 │  - Histogram equalization   │
 └────────┬────────────────────┘
          │  Filtered VoxelVolume
          ▼
 ┌────────────────────────────────┐
 │  4. DETECTION LAYER            │
 │  - Seed selection(HU threshold)│
 │  - 3D region growing (26-conn.)│
 │  - Candidate scoring/filtering │
 └────────┬───────────────────────┘
          │  vector<Anomaly> { centroid, bbox, volume, mean HU }
          ▼
 ┌─────────────────────────────┐
 │  5. OUTPUT LAYER            │
 │  - PNG slice export (libpng)│
 │  - Annotated DICOM re-encode│
 │  - JSON findings report     │
 └─────────────────────────────┘
```

Each arrow is a deliberate ownership boundary: a layer consumes the previous
layer's output type and produces the next layer's input type. No layer reaches
"backward" into an earlier layer's internal state.

---

## 3. Layer 1 — Ingestion Layer

**Scope:** custom binary DICOM parser (not a DCMTK passthrough — DCMTK is used
in Week 5 only to *verify* our own parser's output against a trusted
reference).

### 3.1 File structure assumptions

A DICOM file (Part 10 format) consists of:

```
[128-byte preamble] [ "DICM" magic ] [ File Meta Info group (0002,xxxx) ]
[ Data Set: sequence of (Tag, VR, Length, Value) elements ]
```

### 3.2 Core data structures

```cpp
struct DicomTag {
    uint16_t group;
    uint16_t element;
    bool operator==(const DicomTag&) const = default;
};

enum class VR {
    AE, AS, AT, CS, DA, DS, DT, FL, FD, IS, LO, LT,
    OB, OD, OF, OL, OW, PN, SH, SL, SQ, SS, ST, TM,
    UC, UI, UL, UN, UR, US, UT,
    Implicit  // used when transfer syntax is Implicit VR Little Endian
};

struct DicomElement {
    DicomTag tag;
    VR vr;
    uint32_t length;
    std::vector<std::byte> rawValue;   // owns the bytes; typed accessors decode lazily
};

class DicomDataset {
public:
    std::optional<std::string_view> getString(DicomTag tag) const;
    std::optional<int32_t>          getInt(DicomTag tag) const;
    std::optional<double>           getDouble(DicomTag tag) const;
    const DicomElement*             getRaw(DicomTag tag) const;   // for PixelData etc.

private:
    std::unordered_map<DicomTag, DicomElement> elements_;
};

struct DicomSlice {
    DicomDataset metadata;
    std::vector<uint16_t> pixelData;    // decoded, native endianness
    int rows{};
    int columns{};
    double sliceLocation{};
    double rescaleSlope{1.0};
    double rescaleIntercept{0.0};
};
```

### 3.3 Parsing responsibilities

| Concern | Handling strategy |
|---|---|
| Transfer syntax detection | Read `(0002,0010) TransferSyntaxUID` from File Meta Info (always Explicit VR Little Endian) before parsing the main dataset, since it dictates how the rest of the file is read. |
| Explicit vs. Implicit VR | Explicit VR elements carry a 2-byte VR code in the stream; Implicit VR elements do not and require a static tag→VR dictionary lookup instead. |
| Endianness | Little Endian is the overwhelming default; Big Endian Explicit VR is legacy but must be supported per DICOM PS3.5 — byte-swap on read based on the transfer syntax UID. |
| Long-form VR lengths | `OB, OW, OF, SQ, UT, UN` use a 4-byte length field (with 2 reserved bytes first); all others use 2-byte length. Getting this wrong misaligns every subsequent tag in the file. |
| Sequences (SQ) | Recursive: a sequence contains items, each item is itself a nested dataset. Parsed depth-first. |
| Pixel Data (7FE0,0010) | For uncompressed transfer syntaxes, raw pixel bytes; length may be `0xFFFFFFFF` (undefined length) for encapsulated/compressed pixel data using Basic Offset Table + fragments — out of scope for Week 5, detected and reported as "unsupported" rather than silently mishandled. |

### 3.4 Validation strategy for Week 5

Every sample file (one CT, one MRI, one X-Ray minimum) is run through both:

1. our custom parser, and
2. DCMTK's `DcmFileFormat::loadFile`,

and the extracted `Modality`, `Rows`, `Columns`, `BitsAllocated`,
`PixelData` length, and pixel checksum are diff'd. Divergence here is a
parser bug, full stop — DCMTK is the ground truth for this comparison only.

---

## 4. Layer 2 — Reconstruction Layer

**Input:** `std::vector<DicomSlice>` (one series, unordered).
**Output:** `VoxelVolume` (dense 3D grid, isotropic spacing).

### 4.1 Slice ordering

Sort by `ImagePositionPatient` (0020,0032) projected onto the series'
normal vector — *not* by filename or `InstanceNumber`, which can be
unreliable or absent.

### 4.2 Hounsfield Unit normalization

For CT series, raw pixel values are converted using the per-slice rescale
parameters:

```
HU = pixelValue * RescaleSlope + RescaleIntercept
```

(`RescaleSlope`/`RescaleIntercept` come from tags `(0028,1053)`/`(0028,1052)`
and can legitimately differ slice-to-slice.) MRI/X-Ray series lack a
standardized HU scale; those modalities are stored as normalized intensity
in `[0, 1]` instead, with modality tracked in `VoxelVolume::modality`.

### 4.3 Trilinear interpolation

Slice thickness and pixel spacing are rarely isotropic (e.g., 0.7mm × 0.7mm
in-plane, 3mm between slices). To build a volume with uniform voxel spacing,
each output voxel's HU value is computed as a trilinear blend of the 8
nearest input voxels:

```cpp
struct VoxelVolume {
    int width, height, depth;
    double voxelSpacingMM;         // isotropic, post-interpolation
    std::vector<float> data;       // width*height*depth, row-major
    Modality modality;

    float at(int x, int y, int z) const {
        return data[(z * height + y) * width + x];
    }
};
```

Interpolation is embarrassingly parallel per output voxel and is the first
candidate for thread-pool parallelization (Week 6/7).

---

## 5. Layer 3 — Processing Layer

### 5.1 Thread pool

```cpp
class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency());
    ~ThreadPool();

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>;

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queueMutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_{false};
};
```

Volume-processing tasks are chunked along the Z axis (one task per N slices)
so each worker operates on a contiguous, cache-friendly memory region with
no false sharing between threads.

### 5.2 SIMD filters (AVX2)

| Filter | Approach |
|---|---|
| Gaussian blur | Separable 1D convolution (horizontal then vertical pass), each pass vectorized over 8 `float`s via `__m256`. |
| Edge detection | Sobel operator; gradients computed with `_mm256_mul_ps`/`_mm256_add_ps` across 8-wide lanes, magnitude via `_mm256_sqrt_ps`. |
| Histogram equalization | Histogram bucket accumulation is scalar (data-dependent, hard to vectorize safely); the intensity remapping pass over the volume is vectorized. |

Runtime AVX2 detection (via `__builtin_cpu_supports("avx2")` /
`cpuid` on MSVC) gates SIMD code paths, with a scalar fallback so the binary
doesn't crash with `SIGILL` on non-AVX2 hardware.

---

## 6. Layer 4 — Detection Layer

**Goal:** flag candidate nodules/calcifications as 3D connected regions of
anomalous density.

### 6.1 Seed selection

Voxels whose HU value falls in a suspicious range (e.g., 100–400 HU for
calcifications, tunable per use case) and that are local maxima within a
small neighborhood become seed candidates.

### 6.2 Region growing

26-connectivity (full 3×3×3 neighborhood minus center) flood-fill from each
seed, accepting neighbors within a tolerance band of the seed's HU value:

```cpp
struct Anomaly {
    std::array<int,3> centroid;
    std::array<int,3> bboxMin, bboxMax;
    size_t voxelCount;
    double meanHU;
    double stddevHU;
};

std::vector<Anomaly> regionGrow(const VoxelVolume& volume,
                                 const std::vector<Seed>& seeds,
                                 double huTolerance);
```

Visited voxels are tracked in a `std::vector<bool>` (or bitset) sized to the
volume to avoid revisiting; each region-growing call from an unvisited seed
runs independently, making this parallelizable across the thread pool with
one caveat: two seeds must not race on the shared visited-mask, so seeds are
partitioned into non-overlapping bounding regions before dispatch, or a
mutex-guarded mask is used for the (expected rare) boundary cases.

### 6.3 Post-filtering

Regions below a minimum voxel count (noise) or exceeding a maximum size
(likely a segmentation error, e.g. leaking into surrounding tissue) are
discarded before being reported as findings.

---

## 7. Layer 5 — Output Layer

| Output | Library/approach |
|---|---|
| PNG slice export | libpng, one 2D slice (or a chosen orthogonal plane) at a time, with detected anomalies overlaid as bounding boxes/contours. |
| Annotated DICOM | Re-encode the original `DicomDataset` with a private tag block or a Structured Report (SR) referencing the findings, preserving original pixel data. |
| JSON findings report | One `Anomaly` → one JSON object: `centroid`, `bbox`, `voxelCount`, `meanHU`, `stddevHU`, plus series-level metadata (`PatientID`, `StudyDate`, `Modality`). |

Example JSON shape:

```json
{
  "patientId": "ANON001",
  "modality": "CT",
  "studyDate": "20240115",
  "findings": [
    {
      "id": 1,
      "centroid": [154, 201, 88],
      "boundingBox": { "min": [148, 195, 84], "max": [160, 207, 92] },
      "voxelCount": 342,
      "meanHU": 187.4,
      "stddevHU": 22.1
    }
  ]
}
```

---

## 8. Concurrency Model Summary

- **Ingestion:** one file parsed per thread-pool task; independent, no shared
  mutable state (each `DicomSlice` is owned by its own task until returned).
- **Reconstruction:** interpolation parallelized by output-voxel-range chunks.
- **Processing:** SIMD filters parallelized by Z-slice range.
- **Detection:** region growing parallelized by seed partition, guarded
  visited-mask for boundary overlap.
- **Output:** export tasks (per-slice PNG, JSON serialization) are
  independent and trivially parallel.

`std::atomic<size_t>` counters track pipeline progress for logging;
`std::condition_variable` coordinates the thread pool's task queue; no
layer holds a lock for longer than a queue push/pop.

---

## 9. Build & Dependency Management

- **CMake ≥ 3.20**, C++20, `CMAKE_EXPORT_COMPILE_COMMANDS` on for tooling.
- **Conan 2.x** resolves `dcmtk`, `libpng`, `zlib` via `CMakeDeps` +
  `CMakeToolchain` generators — see `conanfile.txt`.
- AVX2 flags (`-mavx2 -mfma` / `/arch:AVX2`) applied at the target level, not
  globally, so future non-SIMD targets (e.g. a test runner) aren't forced to
  assume AVX2 hardware.
- DCMTK is used in two distinct roles across the project: (a) Week 5
  ground-truth verification of our custom parser, and (b) optionally, DICOM
  re-encoding in the Output Layer, since re-implementing a spec-correct
  writer is lower value than a spec-correct reader for this project's goals.

---

## 10. Testing Strategy

| Layer | Test approach |
|---|---|
| Ingestion | Unit tests per VR type; round-trip byte fixtures for Explicit/Implicit and Little/Big Endian; parser-vs-DCMTK diff on real sample files. |
| Reconstruction | Known-input synthetic volumes (e.g., a linear gradient) to verify interpolation math analytically. |
| Processing | Compare AVX2 filter output against a scalar reference implementation, bit-for-bit or within float epsilon. |
| Detection | Synthetic volumes with planted spheres of known HU/size to verify recall and bounding-box accuracy. |
| Output | JSON schema validation; PNG round-trip (write then re-read, compare pixels). |

Sample DICOM corpus: synthetic/de-identified test files only (e.g., public
datasets from TCIA or DICOM library test suites) — no real patient data
under any circumstance.

---

## 11. Known Limitations & Risks

- **Compressed pixel data** (JPEG/JPEG2000/RLE transfer syntaxes) is out of
  scope for the custom parser; such files are detected and rejected with a
  clear error rather than silently mis-decoded.
- **DCMTK licensing:** DCMTK uses a permissive, non-copyleft custom license
  (not GPL) — compatible with this project's use, but worth citing explicitly
  in any downstream distribution.
- **Patient data privacy:** all sample/test files must be synthetic or
  properly de-identified; this is a hard project constraint, not a
  nice-to-have.
- **Region growing sensitivity:** HU tolerance and minimum-size thresholds
  are currently fixed constants; tuning per modality/anatomy is a known
  future improvement, not a Week 5–7 blocker.
- **Single-machine scope:** no distributed processing; thread pool is
  bounded by `std::thread::hardware_concurrency()` on one machine.

---

## 12. Week-by-Week Traceability

| Week | Layer(s) touched | Deliverable |
|---|---|---|
| 5 | Ingestion | Environment setup, custom binary parser, CT/MRI/X-Ray metadata + pixel extraction |
| 6 | Reconstruction | Slice stacking, HU normalization, trilinear interpolation |
| 7 | Processing | Thread pool, AVX2 filters |
| 8 | Detection | 3D region growing |
| 9 | Output | PNG/DICOM/JSON export, end-to-end pipeline integration |