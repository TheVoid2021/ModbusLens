#include "ui/manual/ManualStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringConverter>
#include <QUuid>

#include "ui/manual/ManualDocxTextExtractor.h"
#include "ui/manual/ManualPdfTextExtractor.h"
#include "ui/manual/ManualTextExtraction.h"

namespace modbuslens::ui {

namespace {

QString g_managedRootOverride;

[[nodiscard]] bool hasPrefix(QByteArrayView data, const char *prefix,
                             qsizetype length)
{
    if (data.size() < length) {
        return false;
    }
    for (qsizetype i = 0; i < length; ++i) {
        if (static_cast<unsigned char>(data[i])
            != static_cast<unsigned char>(prefix[i])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool containsNul(QByteArrayView data)
{
    for (qsizetype i = 0; i < data.size(); ++i) {
        if (data[i] == '\0') {
            return true;
        }
    }
    return false;
}

} // namespace

void ManualStore::setManagedRootOverride(const QString &dir)
{
    g_managedRootOverride = dir;
}

QString ManualStore::managedRootOverride()
{
    return g_managedRootOverride;
}

QString ManualStore::defaultManualsDirectory()
{
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base).filePath(QStringLiteral("manuals"));
}

QString ManualStore::managedManualsDirectory()
{
    const QString root = g_managedRootOverride.isEmpty()
                             ? QStandardPaths::writableLocation(
                                   QStandardPaths::AppDataLocation)
                             : g_managedRootOverride;
    return QDir(root).filePath(QStringLiteral("manuals"));
}

QString ManualStore::documentsDirectory()
{
    return QDir(managedManualsDirectory()).filePath(QStringLiteral("documents"));
}

QString ManualStore::sourceDirectory()
{
    return QDir(managedManualsDirectory()).filePath(QStringLiteral("source"));
}

QString ManualStore::textDirectory()
{
    return QDir(managedManualsDirectory()).filePath(QStringLiteral("text"));
}

bool ManualStore::writeFileAtomic(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        return false;
    }
    return file.commit();
}

QByteArray ManualStore::serializeMetadata(
    const modbuslens::core::ManualDocument &document)
{
    using namespace modbuslens::core;
    QJsonObject root;
    root[QStringLiteral("schemaVersion")] = document.schemaVersion;
    root[QStringLiteral("documentId")] =
        QString::fromStdString(document.documentId);
    root[QStringLiteral("originalFileName")] =
        QString::fromStdString(document.originalFileName);
    root[QStringLiteral("documentType")] =
        QString::fromStdString(
            std::string(manualDocumentTypeToken(document.documentType)));
    root[QStringLiteral("originalPath")] =
        QString::fromStdString(document.originalPath);
    root[QStringLiteral("contentHash")] =
        QString::fromStdString(document.contentHash);
    root[QStringLiteral("byteSize")] =
        static_cast<qint64>(document.byteSize);
    root[QStringLiteral("charCount")] =
        static_cast<qint64>(document.charCount);
    root[QStringLiteral("statusToken")] =
        QString::fromStdString(std::string(
            manualDocumentStatusToken(document.status)));
    // M12-C C1b second slice (T027 §60.2): "extracted" |
    // "no_extractable_text". Written for every format so the schema stays
    // uniform (TXT / Markdown are always "extracted").
    root[QStringLiteral("extractionStateToken")] =
        QString::fromStdString(document.extractionStateToken);
    QJsonDocument json(root);
    // QJsonDocument::Compact + QJsonObject key ordering make the metadata
    // byte-stable, so a save is reproducible and diffable.
    return json.toJson(QJsonDocument::Compact);
}

ManualImportResult ManualStore::importSourceFile(const QString &sourcePath,
                                                 const QString &originalFileName)
{
    using namespace modbuslens::core;

    ManualImportResult result;

    // 1. Type: extension-driven, and the STORE validates it independently of
    //    any UI file-dialog filter.
    ManualDocumentType type{ManualDocumentType::Txt};
    if (!manualDocumentTypeFromExtension(originalFileName.toStdString(), type)) {
        result.error = ManualImportError::UnsupportedType;
        return result;
    }

    // 2. Existence + resource limit BEFORE reading anything into memory.
    const QFileInfo info(sourcePath);
    if (!info.exists() || !info.isFile()) {
        result.error = ManualImportError::FileNotFound;
        return result;
    }
    if (static_cast<std::uint64_t>(info.size()) > kManualMaxSourceBytes) {
        result.error = ManualImportError::TooLarge;
        return result;
    }

    QFile file(sourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = ManualImportError::ReadFailed;
        return result;
    }
    const QByteArray bytes = file.readAll();
    file.close();
    if (bytes.isEmpty()) {
        result.error = ManualImportError::EmptyContent;
        return result;
    }

    // 3. Content identity — the ONLY cache key (unchanged from C1a).
    const QString contentHash =
        QString::fromUtf8(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)
                .toHex());

    // 4. Format-specific validation / extraction. NOTHING has been written at
    //    this point, so any failure here keeps the store untouched (atomicity
    //    owner: this function — T027 §60.2).
    //
    //    TXT / Markdown: the C1a path, BYTE-FOR-BYTE unchanged semantics
    //    (BOM discrimination, binary refusal, strict UTF-8, limits).
    //    PDF / DOCX (C1b second slice): routed to the frozen extractors; a PDF
    //    without an existing text layer is an IMPORT SUCCESS recorded as
    //    no_extractable_text (never OCR'd, never guessed), while corrupt /
    //    fake / malformed / encrypted / over-limit inputs make the WHOLE
    //    IMPORT fail.
    QString text;                 // TXT / Markdown / DOCX cache payload
    QStringList pdfPages;         // PDF per-page truth (presentation-agnostic)
    bool isPdf = type == ManualDocumentType::Pdf;
    std::size_t charCount = 0;
    std::string extractionState(
        std::string(manualExtractionStateToken(
            ManualExtractionState::Extracted)));

    if (type == ManualDocumentType::Txt
        || type == ManualDocumentType::Markdown) {
        QByteArrayView body(bytes);
        static const char kUtf8Bom[] = {'\xEF', '\xBB', '\xBF'};
        static const char kUtf16LeBom[] = {'\xFF', '\xFE'};
        static const char kUtf16BeBom[] = {'\xFE', '\xFF'};
        if (hasPrefix(bytes, kUtf8Bom, 3)) {
            body = body.mid(3);
        } else if (hasPrefix(bytes, kUtf16LeBom, 2)
                   || hasPrefix(bytes, kUtf16BeBom, 2)) {
            result.error = ManualImportError::UnsupportedEncoding;
            return result;
        }

        // Binary heuristic (implementation detail): NUL-containing content is
        // not text. It can never misfire on legal Chinese / emoji / other
        // non-ASCII UTF-8, which never contains a NUL byte.
        if (containsNul(body)) {
            result.error = ManualImportError::BinaryContent;
            return result;
        }

        // STRICT UTF-8: a decoder that reports invalid characters instead of a
        // path that silently inserts U+FFFD and still claims success.
        QStringDecoder decoder(QStringConverter::Utf8);
        text = decoder.decode(body);
        const auto finalized = decoder.finalize();
        if (decoder.hasError() || finalized.invalidChars != 0) {
            result.error = ManualImportError::InvalidEncoding;
            return result;
        }
        if (text.isEmpty()) {
            result.error = ManualImportError::EmptyContent;
            return result;
        }
        if (static_cast<std::size_t>(text.size()) > kManualMaxTextChars) {
            result.error = ManualImportError::TooLarge;
            return result;
        }
        charCount = static_cast<std::size_t>(text.size());
    } else {
        const ManualExtractionResult extraction =
            isPdf ? ManualPdfTextExtractor::extractFromFile(sourcePath)
                  : ManualDocxTextExtractor::extractFromBytes(
                        bytes, originalFileName);
        switch (extraction.status) {
        case ManualExtractionStatus::Ok:
            extractionState = std::string(
                manualExtractionStateToken(ManualExtractionState::Extracted));
            break;
        case ManualExtractionStatus::NoExtractableText:
            // HUMAN-APPROVED (T027 §60.2): the import itself SUCCEEDS; the
            // record keeps the managed copy and carries the explicit state.
            extractionState = std::string(
                manualExtractionStateToken(
                    ManualExtractionState::NoExtractableText));
            break;
        case ManualExtractionStatus::MalformedDocument:
            result.error = ManualImportError::MalformedContent;
            return result;
        case ManualExtractionStatus::EncryptedOrPasswordProtected:
            result.error = ManualImportError::EncryptedOrPasswordProtected;
            return result;
        case ManualExtractionStatus::ResourceLimitExceeded:
            result.error = ManualImportError::TooLarge;
            return result;
        case ManualExtractionStatus::InternalError:
        case ManualExtractionStatus::DependencyError:
        case ManualExtractionStatus::UnsupportedDocumentFeature:
        default:
            result.error = ManualImportError::StorageFailed;
            return result;
        }
        if (isPdf) {
            pdfPages = extraction.pageTexts;
            for (const QString &page : pdfPages) {
                charCount += static_cast<std::size_t>(page.size());
            }
        } else {
            text = extraction.mainStoryText;
            charCount = static_cast<std::size_t>(text.size());
        }
        result.document.extractionStateToken = extractionState;
    }

    // 5. Commit: managed copy, text cache, then the metadata index. Nothing
    //    has been written before this point, so a failure up to here cannot
    //    leave a partial document.
    const QDir manualsDir(managedManualsDirectory());
    if (!manualsDir.mkpath(QStringLiteral("documents"))
        || !manualsDir.mkpath(QStringLiteral("source"))
        || !manualsDir.mkpath(QStringLiteral("text"))) {
        result.error = ManualImportError::StorageFailed;
        return result;
    }

    const QString managedCopyPath =
        QDir(sourceDirectory()).filePath(contentHash + QStringLiteral(".bin"));
    // Cache identity is contentHash for every format; the PDF per-page truth
    // lives in a .json sibling so the presentation can page through it while
    // the extractor's per-page truth stays intact (T027 §60.4).
    const bool pdfCache = isPdf;
    const QString cachePath =
        QDir(textDirectory())
            .filePath(contentHash
                      + (pdfCache ? QStringLiteral(".json")
                                  : QStringLiteral(".txt")));
    const bool copyExisted = QFileInfo::exists(managedCopyPath);
    const bool cacheExisted = QFileInfo::exists(cachePath);

    if (!copyExisted && !writeFileAtomic(managedCopyPath, bytes)) {
        result.error = ManualImportError::StorageFailed;
        return result;
    }
    QByteArray cacheBytes;
    if (pdfCache) {
        QJsonArray pagesArray;
        for (const QString &page : pdfPages) {
            pagesArray.append(page);
        }
        QJsonObject cacheRoot;
        cacheRoot[QStringLiteral("schemaVersion")] = 1;
        cacheRoot[QStringLiteral("pages")] = pagesArray;
        cacheBytes = QJsonDocument(cacheRoot).toJson(QJsonDocument::Compact);
    } else {
        cacheBytes = text.toUtf8();
    }
    if (!cacheExisted && !writeFileAtomic(cachePath, cacheBytes)) {
        if (!copyExisted) {
            QFile::remove(managedCopyPath);
        }
        result.error = ManualImportError::StorageFailed;
        return result;
    }

    ManualDocument document;
    document.schemaVersion = 1;
    document.documentId =
        QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    document.originalFileName = originalFileName.toStdString();
    document.documentType = type;
    document.originalPath = sourcePath.toStdString();
    document.contentHash = contentHash.toStdString();
    document.byteSize = static_cast<std::uint64_t>(bytes.size());
    document.charCount = charCount;
    document.status = ManualDocumentStatus::Ready;
    document.extractionStateToken = extractionState;

    const QString metaPath = QDir(documentsDirectory())
                                 .filePath(QString::fromStdString(
                                               document.documentId)
                                           + QStringLiteral(".json"));
    if (!writeFileAtomic(metaPath, serializeMetadata(document))) {
        if (!copyExisted) {
            QFile::remove(managedCopyPath);
        }
        if (!cacheExisted) {
            QFile::remove(cachePath);
        }
        result.error = ManualImportError::StorageFailed;
        return result;
    }

    result.document = document;
    result.error = ManualImportError::Ok;
    return result;
}

std::vector<modbuslens::core::ManualDocument> ManualStore::loadAll()
{
    using namespace modbuslens::core;

    std::vector<ManualDocument> documents;
    const QDir dir(documentsDirectory());
    if (!dir.exists()) {
        return documents;
    }
    const QStringList files =
        dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString &fileName : files) {
        QFile file(dir.filePath(fileName));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (!json.isObject()) {
            continue;
        }
        const QJsonObject root = json.object();
        if (root.value(QStringLiteral("schemaVersion")).toInt(-1) != 1) {
            continue;
        }
        ManualDocument document;
        document.schemaVersion = 1;
        document.documentId =
            root.value(QStringLiteral("documentId")).toString().toStdString();
        document.originalFileName =
            root.value(QStringLiteral("originalFileName")).toString()
                .toStdString();
        ManualDocumentType type{ManualDocumentType::Txt};
        const QString typeToken =
            root.value(QStringLiteral("documentType")).toString();
        if (typeToken == QStringLiteral("md")) {
            type = ManualDocumentType::Markdown;
        } else if (typeToken == QStringLiteral("pdf")) {
            type = ManualDocumentType::Pdf;
        } else if (typeToken == QStringLiteral("docx")) {
            type = ManualDocumentType::Docx;
        }
        document.documentType = type;
        document.originalPath =
            root.value(QStringLiteral("originalPath")).toString().toStdString();
        document.contentHash =
            root.value(QStringLiteral("contentHash")).toString().toStdString();
        document.byteSize = static_cast<std::uint64_t>(
            root.value(QStringLiteral("byteSize")).toVariant().toULongLong());
        document.charCount = static_cast<std::size_t>(
            root.value(QStringLiteral("charCount")).toVariant().toULongLong());
        // M12-C C1b second slice: pre-amendment records (C1a era) carry no
        // extractionStateToken and are always "extracted" — their behaviour is
        // byte-for-byte what it always was.
        document.extractionStateToken =
            root.value(QStringLiteral("extractionStateToken"))
                .toString(QStringLiteral("extracted"))
                .toStdString();
        document.status = hasManagedArtifacts(document)
                              ? ManualDocumentStatus::Ready
                              : ManualDocumentStatus::Unavailable;
        document.statusToken =
            std::string(manualDocumentStatusToken(document.status));
        documents.push_back(document);
    }
    return documents;
}

bool ManualStore::hasManagedArtifacts(
    const modbuslens::core::ManualDocument &document)
{
    const QString hash = QString::fromStdString(document.contentHash);
    if (hash.isEmpty()) {
        return false;
    }
    if (!QFileInfo::exists(
            QDir(sourceDirectory()).filePath(hash + QStringLiteral(".bin")))) {
        return false;
    }
    // The cache artefact is format-specific: PDF per-page truth lives in a
    // .json sibling, every other format keeps the UTF-8 .txt cache.
    const QString cacheName =
        hash + (document.documentType
                        == modbuslens::core::ManualDocumentType::Pdf
                    ? QStringLiteral(".json")
                    : QStringLiteral(".txt"));
    return QFileInfo::exists(QDir(textDirectory()).filePath(cacheName));
}

QString ManualStore::loadText(const QString &contentHash, bool *ok)
{
    if (ok) {
        *ok = false;
    }
    if (contentHash.isEmpty()) {
        return {};
    }
    QFile file(QDir(textDirectory())
                   .filePath(contentHash + QStringLiteral(".txt")));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray bytes = file.readAll();
    file.close();
    if (ok) {
        *ok = true;
    }
    return QString::fromUtf8(bytes);
}

QStringList ManualStore::loadPdfPages(const QString &contentHash, bool *ok)
{
    if (ok) {
        *ok = false;
    }
    QStringList pages;
    if (contentHash.isEmpty()) {
        return pages;
    }
    QFile file(QDir(textDirectory())
                   .filePath(contentHash + QStringLiteral(".json")));
    if (!file.open(QIODevice::ReadOnly)) {
        return pages;
    }
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!json.isObject()) {
        return pages;
    }
    const QJsonArray array = json.object().value(QStringLiteral("pages")).toArray();
    for (const QJsonValue &value : array) {
        pages.append(value.toString());
    }
    if (ok) {
        *ok = true;
    }
    return pages;
}

} // namespace modbuslens::ui
