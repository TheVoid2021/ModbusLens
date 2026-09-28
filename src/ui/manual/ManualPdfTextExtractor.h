#pragma once

#include <QString>

#include "ui/manual/ManualTextExtraction.h"

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// M12-C C1b: deterministic PDF text-layer extraction through the PDFium public C API.
//
// HUMAN-APPROVED PRODUCT DECISION (T027 §58.2): extract the EXISTING text layer only, treat
// PDFium text as UTF-16 and convert it to Unicode/QString, and OCR is DEFERRED — this class
// never rasterizes, guesses or synthesizes text.
//
// Mechanisms NOT used on purpose: no OCR, no PDF rendering, no AI extraction, no online
// conversion, and no GetProcAddress fallback (the direct import-library route passed the
// dependency probe, which per the authorization is the mechanism of record).
//
// Result shape: per-page texts only. This slice deliberately does not freeze any
// user-visible page concatenation / page separator semantics (T027 §58.6).
// ---------------------------------------------------------------------------

class ManualPdfTextExtractor
{
public:
    // Reads/loads the PDF from a filesystem path (the path is handed to the PDFium public
    // API as UTF-8, which is its documented path encoding).
    [[nodiscard]] static ManualExtractionResult extractFromFile(const QString &path);
};

} // namespace modbuslens::ui
