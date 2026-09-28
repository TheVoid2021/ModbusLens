#include "ui/manual/ManualDocxTextExtractor.h"

#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>

#include <zip.h>

namespace modbuslens::ui {

namespace {

constexpr char kRelationshipsNamespace[] =
    "http://schemas.openxmlformats.org/package/2006/relationships";
constexpr char kOfficeDocumentRelationshipType[] =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument";
constexpr char kWordprocessingNamespace[] =
    "http://schemas.openxmlformats.org/wordprocessingml/2006/main";
constexpr char kPackageRelationshipsEntry[] = "_rels/.rels";

class ZipArchiveHandle
{
public:
    explicit ZipArchiveHandle(zip_t *archive) : archive_(archive) {}
    ~ZipArchiveHandle()
    {
        if (archive_ != nullptr) {
            zip_close(archive_);
        }
    }

    [[nodiscard]] zip_t *get() const { return archive_; }

    ZipArchiveHandle(const ZipArchiveHandle &) = delete;
    ZipArchiveHandle &operator=(const ZipArchiveHandle &) = delete;

private:
    zip_t *archive_{nullptr};
};

[[nodiscard]] ManualExtractionResult failure(ManualExtractionStatus status,
                                             const char *token,
                                             qint64 rawError = 0)
{
    ManualExtractionResult result;
    result.status = status;
    result.diagnosticToken = QString::fromLatin1(token);
    result.rawDependencyError = rawError;
    return result;
}

// Package-safe target validation: the relationship target must stay inside the package.
[[nodiscard]] bool isSafePackageTarget(const QString &target, const QString &out)
{
    if (target.isEmpty()) {
        return false;
    }
    // Absolute package path (leading '/') is not accepted by this v1 contract.
    if (target.startsWith(QLatin1Char('/')) || target.startsWith(QLatin1Char('\\'))) {
        return false;
    }
    // Drive-qualified path (for example "C:...") is not a package part.
    if (target.size() >= 2 && target.at(1) == QLatin1Char(':')) {
        return false;
    }
    const QStringList segments = target.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &segment : segments) {
        if (segment == QLatin1String("..")) {
            return false;
        }
    }
    return !out.isEmpty();
}

[[nodiscard]] QString normalizeTarget(QString target)
{
    target.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (target.startsWith(QLatin1Char('/'))) {
        target.remove(0, 1);
    }
    return target;
}

[[nodiscard]] bool readEntry(zip_t *archive, const QString &name, QByteArray *out,
                             qint64 maxBytes)
{
    const QByteArray encoded = name.toUtf8();
    zip_stat_t stat;
    zip_stat_init(&stat);
    if (zip_stat(archive, encoded.constData(), 0, &stat) != 0) {
        return false;
    }
    if (maxBytes > 0 && static_cast<qint64>(stat.size) > maxBytes) {
        return false;
    }
    zip_file_t *file = zip_fopen(archive, encoded.constData(), 0);
    if (file == nullptr) {
        return false;
    }
    QByteArray buffer;
    buffer.resize(static_cast<int>(stat.size));
    const zip_int64_t got = zip_fread(file, buffer.data(), stat.size);
    zip_fclose(file);
    if (got < 0 || static_cast<zip_uint64_t>(got) != stat.size) {
        return false;
    }
    *out = buffer;
    return true;
}

// Relationship discovery — namespace aware, never prefix dependent.
[[nodiscard]] bool findMainDocumentPart(zip_t *archive, QString *partOut, const char **why)
{
    QByteArray relationshipBytes;
    if (!readEntry(archive, QString::fromLatin1(kPackageRelationshipsEntry), &relationshipBytes,
                   0)) {
        *why = "package_relationships_missing";
        return false;
    }
    QXmlStreamReader xml(relationshipBytes);
    QString target;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) {
            continue;
        }
        if (xml.name() != QLatin1String("Relationship")
            || xml.namespaceUri() != QLatin1String(kRelationshipsNamespace)) {
            continue;
        }
        const QXmlStreamAttributes attributes = xml.attributes();
        if (attributes.value(QLatin1String("Type"))
            == QLatin1String(kOfficeDocumentRelationshipType)) {
            target = attributes.value(QLatin1String("Target")).toString();
            break;
        }
    }
    if (xml.hasError()) {
        *why = "package_relationships_malformed";
        return false;
    }
    if (target.isEmpty()) {
        *why = "office_document_relationship_missing";
        return false;
    }
    // Raw target first: normalizing before validating would launder an absolute or
    // drive-qualified target into a seemingly safe relative one.
    QString validated = target;
    if (!isSafePackageTarget(target, validated)) {
        *why = "unsafe_relationship_target";
        return false;
    }
    *partOut = normalizeTarget(target);
    return true;
}

// Frozen plain-text semantics (T027 §58.3). Namespace aware; the "w" prefix is never assumed.
[[nodiscard]] bool extractMainStory(const QByteArray &xmlBytes, QString *textOut,
                                    const char **why)
{
    QXmlStreamReader xml(xmlBytes);
    const QString wordNamespace = QString::fromLatin1(kWordprocessingNamespace);
    QString text;
    bool inBody = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            if (xml.namespaceUri() != wordNamespace) {
                continue;
            }
            const QStringView name = xml.name();
            if (name == QLatin1String("body")) {
                inBody = true;
            } else if (!inBody) {
                continue;
            } else if (name == QLatin1String("t")) {
                text += xml.readElementText(QXmlStreamReader::IncludeChildElements);
                continue;
            } else if (name == QLatin1String("tab")) {
                text += QLatin1Char('\t');
            } else if (name == QLatin1String("br") || name == QLatin1String("cr")) {
                text += QLatin1Char('\n');
            }
        } else if (xml.isEndElement()) {
            if (xml.namespaceUri() != wordNamespace) {
                continue;
            }
            const QStringView name = xml.name();
            if (name == QLatin1String("body")) {
                inBody = false;
            } else if (!inBody) {
                continue;
            } else if (name == QLatin1String("p")) {
                text += QLatin1Char('\n');
            } else if (name == QLatin1String("tc")) {
                text += QLatin1Char('\t');
            } else if (name == QLatin1String("tr")) {
                text += QLatin1Char('\n');
            }
        }
    }
    if (xml.hasError()) {
        *why = "main_document_xml_malformed";
        return false;
    }
    *textOut = text;
    return true;
}

} // namespace

ManualExtractionResult ManualDocxTextExtractor::extractFromFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return failure(ManualExtractionStatus::MalformedDocument, "source_missing");
    }
    if (info.size() > kManualExtractionMaxSourceBytes) {
        return failure(ManualExtractionStatus::ResourceLimitExceeded, "source_too_large");
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return failure(ManualExtractionStatus::InternalError, "source_unreadable");
    }
    const QByteArray bytes = file.readAll();
    file.close();
    return extractFromBytes(bytes, info.fileName());
}

ManualExtractionResult ManualDocxTextExtractor::extractFromBytes(const QByteArray &bytes,
                                                                const QString &)
{
    if (bytes.isEmpty()) {
        return failure(ManualExtractionStatus::MalformedDocument, "empty_source");
    }
    if (bytes.size() > kManualExtractionMaxSourceBytes) {
        return failure(ManualExtractionStatus::ResourceLimitExceeded, "source_too_large");
    }

    zip_error_t error;
    zip_error_init(&error);
    zip_source_t *source = zip_source_buffer_create(bytes.constData(),
                                                   static_cast<zip_uint64_t>(bytes.size()),
                                                   0, &error);
    if (source == nullptr) {
        zip_error_fini(&error);
        return failure(ManualExtractionStatus::DependencyError, "zip_source_failed");
    }
    // Ownership contract: on success the archive owns the source (freed by zip_close); on
    // failure the caller still owns it and must free it (libzip zip_open_from_source(3)).
    ZipArchiveHandle archive(zip_open_from_source(source, ZIP_RDONLY, &error));
    if (archive.get() == nullptr) {
        const qint64 raw = zip_error_code_zip(&error);
        zip_error_fini(&error);
        zip_source_free(source);
        return failure(ManualExtractionStatus::MalformedDocument, "not_a_zip_container", raw);
    }
    zip_error_fini(&error);

    const zip_int64_t entryCount = zip_get_num_entries(archive.get(), 0);
    if (entryCount < 0) {
        return failure(ManualExtractionStatus::MalformedDocument, "entry_count_failed");
    }
    if (entryCount > kManualDocxMaxZipEntries) {
        return failure(ManualExtractionStatus::ResourceLimitExceeded, "too_many_zip_entries");
    }
    // ZIP-bomb accounting over the central directory.
    qint64 aggregate = 0;
    for (zip_uint64_t index = 0; index < static_cast<zip_uint64_t>(entryCount); ++index) {
        zip_stat_t stat;
        zip_stat_init(&stat);
        if (zip_stat_index(archive.get(), index, 0, &stat) != 0) {
            continue;
        }
        const qint64 size = static_cast<qint64>(stat.size);
        if (size > kManualDocxMaxEntryUncompressedBytes) {
            return failure(ManualExtractionStatus::ResourceLimitExceeded,
                           "zip_entry_too_large");
        }
        aggregate += size;
        if (aggregate > kManualDocxMaxAggregateUncompressedBytes) {
            return failure(ManualExtractionStatus::ResourceLimitExceeded,
                           "zip_aggregate_too_large");
        }
    }

    QString mainPart;
    const char *why = nullptr;
    if (!findMainDocumentPart(archive.get(), &mainPart, &why)) {
        return failure(ManualExtractionStatus::MalformedDocument, why);
    }

    QByteArray mainXml;
    if (!readEntry(archive.get(), mainPart, &mainXml, kManualDocxMaxMainXmlBytes)) {
        return failure(ManualExtractionStatus::MalformedDocument, "main_document_part_missing");
    }

    QString story;
    if (!extractMainStory(mainXml, &story, &why)) {
        return failure(ManualExtractionStatus::MalformedDocument, why);
    }
    if (story.size() > kManualExtractionMaxTextChars) {
        return failure(ManualExtractionStatus::ResourceLimitExceeded, "text_too_large");
    }

    ManualExtractionResult result;
    result.mainStoryText = story;
    result.totalChars = story.size();
    result.pageCount = 1;
    if (story.isEmpty()) {
        result.status = ManualExtractionStatus::NoExtractableText;
        result.diagnosticToken = QStringLiteral("empty_main_story");
        return result;
    }
    result.status = ManualExtractionStatus::Ok;
    return result;
}

} // namespace modbuslens::ui
