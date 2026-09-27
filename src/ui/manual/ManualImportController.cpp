#include "ui/manual/ManualImportController.h"

#include <QFileInfo>
#include <QVariant>

#include <algorithm>

namespace modbuslens::ui {

namespace {

[[nodiscard]] QString humanErrorText(modbuslens::core::ManualImportError error)
{
    using modbuslens::core::ManualImportError;
    switch (error) {
    case ManualImportError::Ok:
        return QStringLiteral("导入成功。");
    case ManualImportError::UnsupportedType:
        return QStringLiteral(
            "不支持的文件类型：C1a 仅支持 TXT / Markdown（PDF / DOCX 属于后续 C1b）。");
    case ManualImportError::FileNotFound:
        return QStringLiteral("文件不存在或不是普通文件。");
    case ManualImportError::ReadFailed:
        return QStringLiteral("无法读取文件。");
    case ManualImportError::TooLarge:
        return QStringLiteral("文件超出导入上限，已拒绝（不做截断）。");
    case ManualImportError::BinaryContent:
        return QStringLiteral("内容疑似二进制文件，已拒绝。");
    case ManualImportError::InvalidEncoding:
        return QStringLiteral("不是合法的 UTF-8 文本，已拒绝。");
    case ManualImportError::UnsupportedEncoding:
        return QStringLiteral("检测到 UTF-16 编码：C1a 暂不支持（非永久排除）。");
    case ManualImportError::EmptyContent:
        return QStringLiteral("文件内容为空。");
    case ManualImportError::StorageFailed:
        return QStringLiteral("写入受管存储失败，已回滚。");
    }
    return QStringLiteral("导入失败。");
}

[[nodiscard]] QVariantMap toVariantMap(
    const modbuslens::core::ManualDocument &document)
{
    using namespace modbuslens::core;
    QVariantMap map;
    map[QStringLiteral("documentId")] =
        QString::fromStdString(document.documentId);
    map[QStringLiteral("originalFileName")] =
        QString::fromStdString(document.originalFileName);
    map[QStringLiteral("documentType")] = QString::fromStdString(
        std::string(manualDocumentTypeToken(document.documentType)));
    map[QStringLiteral("originalPath")] =
        QString::fromStdString(document.originalPath);
    map[QStringLiteral("contentHash")] =
        QString::fromStdString(document.contentHash);
    map[QStringLiteral("byteSize")] =
        static_cast<qlonglong>(document.byteSize);
    map[QStringLiteral("charCount")] =
        static_cast<qlonglong>(document.charCount);
    map[QStringLiteral("statusToken")] = QString::fromStdString(
        std::string(manualDocumentStatusToken(document.status)));
    return map;
}

} // namespace

ManualImportController::ManualImportController(QObject *parent)
    : QObject(parent)
{
}

QString ManualImportController::scopeText() const
{
    return QStringLiteral(
        "C1a：支持 TXT / Markdown（UTF-8 / UTF-8 BOM）。PDF / DOCX 属于后续 C1b，OCR 为后续能力。");
}

QVariantList ManualImportController::manualDocuments() const
{
    QVariantList list;
    for (const auto &document : m_documents) {
        list << toVariantMap(document);
    }
    return list;
}

int ManualImportController::selectedIndex() const
{
    return m_selectedIndex;
}

QVariantMap ManualImportController::selectedDocument() const
{
    if (m_selectedIndex < 0
        || m_selectedIndex >= static_cast<int>(m_documents.size())) {
        return {};
    }
    return toVariantMap(m_documents.at(static_cast<std::size_t>(
        m_selectedIndex)));
}

QString ManualImportController::previewText() const
{
    return m_previewText;
}

QString ManualImportController::lastErrorText() const
{
    return m_lastErrorText;
}

QString ManualImportController::lastErrorToken() const
{
    return m_lastErrorToken;
}

void ManualImportController::setError(
    modbuslens::core::ManualImportError error)
{
    using namespace modbuslens::core;
    m_lastErrorText = humanErrorText(error);
    m_lastErrorToken =
        QString::fromStdString(std::string(manualImportErrorToken(error)));
}

void ManualImportController::clearError()
{
    m_lastErrorText.clear();
    m_lastErrorToken.clear();
}

bool ManualImportController::importManualFile(const QUrl &fileUrl)
{
    if (!fileUrl.isLocalFile()) {
        setError(modbuslens::core::ManualImportError::FileNotFound);
        emit documentsChanged();
        return false;
    }
    const QString filePath = fileUrl.toLocalFile();
    const QFileInfo info(filePath);
    const ManualImportResult result =
        ManualStore::importSourceFile(filePath, info.fileName());
    if (!result.ok()) {
        setError(result.error);
        emit documentsChanged();
        return false;
    }
    clearError();
    refresh();
    // Select the freshly imported document (the newest metadata file) so the
    // Human immediately sees what was imported.
    m_selectedIndex = -1;
    for (std::size_t i = 0; i < m_documents.size(); ++i) {
        if (m_documents[i].documentId == result.document.documentId) {
            m_selectedIndex = static_cast<int>(i);
            break;
        }
    }
    m_previewText.clear();
    bool ok = false;
    m_previewText = ManualStore::loadText(
        QString::fromStdString(result.document.contentHash), &ok);
    if (!ok) {
        m_previewText.clear();
    }
    emit selectionChanged();
    return true;
}

void ManualImportController::selectDocument(int index)
{
    if (index < 0 || index >= static_cast<int>(m_documents.size())) {
        m_selectedIndex = -1;
        m_previewText.clear();
        emit selectionChanged();
        return;
    }
    m_selectedIndex = index;
    bool ok = false;
    m_previewText = ManualStore::loadText(
        QString::fromStdString(
            m_documents.at(static_cast<std::size_t>(index)).contentHash),
        &ok);
    if (!ok) {
        m_previewText.clear();
    }
    emit selectionChanged();
}

void ManualImportController::clearSelection()
{
    m_selectedIndex = -1;
    m_previewText.clear();
    emit selectionChanged();
}

void ManualImportController::refresh()
{
    m_documents = ManualStore::loadAll();
    if (m_selectedIndex >= static_cast<int>(m_documents.size())) {
        m_selectedIndex = -1;
        m_previewText.clear();
    }
    emit documentsChanged();
    emit selectionChanged();
}

QVariantMap ManualImportController::evidenceReferenceAt(int start,
                                                        int end) const
{
    using namespace modbuslens::core;
    QVariantMap map;
    if (m_selectedIndex < 0
        || m_selectedIndex >= static_cast<int>(m_documents.size())) {
        return map;
    }
    const ManualDocument &document =
        m_documents.at(static_cast<std::size_t>(m_selectedIndex));
    const int length = m_previewText.size();
    int from = start < 0 ? 0 : start;
    int to = end < 0 ? length : end;
    if (from > length) {
        from = length;
    }
    if (to > length) {
        to = length;
    }
    if (to < from) {
        to = from;
    }
    QString excerpt = m_previewText.mid(from, to - from);
    if (excerpt.size() > kManualMaxExcerptChars) {
        excerpt = excerpt.left(kManualMaxExcerptChars);
    }
    map[QStringLiteral("documentId")] =
        QString::fromStdString(document.documentId);
    map[QStringLiteral("contentHash")] =
        QString::fromStdString(document.contentHash);
    // A page number is NEVER fabricated: TXT / Markdown have none.
    map[QStringLiteral("pageNumber")] = -1;
    map[QStringLiteral("section")] = QString();
    map[QStringLiteral("textStart")] = from;
    map[QStringLiteral("textEnd")] = to;
    map[QStringLiteral("excerpt")] = excerpt;
    return map;
}

} // namespace modbuslens::ui
