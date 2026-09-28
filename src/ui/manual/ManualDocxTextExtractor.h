#pragma once

#include <QByteArray>
#include <QString>

#include "ui/manual/ManualTextExtraction.h"

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// M12-C C1b: deterministic DOCX (OOXML) main-story plain-text extraction.
//
// Frozen route (HUMAN-APPROVED): libzip for the ZIP container + Qt Core QXmlStreamReader
// for XML — no second XML parser, no Office/LibreOffice automation, no Python/Java runtime.
//
// Frozen semantics (T027 §58.3, HUMAN-APPROVED PRODUCT DECISION):
//   paragraph = "\n" · table cell = "\t" · table row = "\n" · w:tab = "\t" ·
//   w:br / w:cr = "\n" · no separator between runs · w:t verbatim honouring xml:space.
// Nothing is trimmed, collapsed, beautified, rendered or evaluated.
//
// Story boundary (v1): the Main Document Part discovered through the PACKAGE RELATIONSHIP
// (_rels/.rels -> .../officeDocument). word/document.xml is never hard-coded. Headers,
// footers, comments, footnotes, endnotes, embedded objects and tracked changes live in other
// parts and therefore cannot leak into this result.
// ---------------------------------------------------------------------------

class ManualDocxTextExtractor
{
public:
    [[nodiscard]] static ManualExtractionResult extractFromFile(const QString &path);
    // Bytes entry: Qt reads the file (Unicode-path safe) and libzip consumes a memory source,
    // so libzip never has to resolve a non-ASCII path itself.
    [[nodiscard]] static ManualExtractionResult extractFromBytes(const QByteArray &bytes,
                                                                 const QString &sourceName);
};

} // namespace modbuslens::ui
