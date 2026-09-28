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
            "不支持的文件类型：仅支持 TXT / Markdown / PDF / DOCX。");
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
        return QStringLiteral("检测到 UTF-16 编码：暂不支持（非永久排除）。");
    case ManualImportError::EmptyContent:
        return QStringLiteral("文件内容为空。");
    case ManualImportError::StorageFailed:
        return QStringLiteral("写入受管存储失败，已回滚。");
    case ManualImportError::MalformedContent:
        return QStringLiteral(
            "文件已损坏或不是有效的 PDF / DOCX 文档，整个导入已回滚。");
    case ManualImportError::EncryptedOrPasswordProtected:
        return QStringLiteral(
            "文档已加密或受密码保护，已拒绝（不做破解尝试）。");
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
    // M12-C C1b second slice (T027 §60.2): "extracted" |
    // "no_extractable_text" — the UI must show an explicit no-text state.
    map[QStringLiteral("extractionStateToken")] =
        QString::fromStdString(document.extractionStateToken);
    return map;
}

// PDF presentation only (T027 §60.4): the page headers are built here, at
// display time — they are NEVER part of the cached per-page truth.
[[nodiscard]] QString buildPdfPreview(const QStringList &pages)
{
    QString preview;
    for (int index = 0; index < pages.size(); ++index) {
        if (index > 0) {
            preview += QStringLiteral("\n\n");
        }
        preview += QStringLiteral("第 %1 页\n").arg(index + 1);
        preview += pages.at(index);
    }
    return preview;
}

} // namespace

ManualImportController::ManualImportController(QObject *parent)
    : QObject(parent)
{
}

QString ManualImportController::scopeText() const
{
    return QStringLiteral(
        "支持 TXT / Markdown（UTF-8 / UTF-8 BOM）与 PDF / DOCX（PDF 仅提取已有文本层，"
        "无 OCR；DOCX 仅提取正文）。");
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

QString ManualImportController::previewStateToken() const
{
    return m_previewStateToken;
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

void ManualImportController::refreshPreview()
{
    m_previewText.clear();
    m_previewStateToken = QStringLiteral("text");
    if (m_selectedIndex < 0
        || m_selectedIndex >= static_cast<int>(m_documents.size())) {
        return;
    }
    const modbuslens::core::ManualDocument &document =
        m_documents.at(static_cast<std::size_t>(m_selectedIndex));
    const QString contentHash =
        QString::fromStdString(document.contentHash);

    if (document.extractionStateToken
        == std::string(modbuslens::core::manualExtractionStateToken(
               modbuslens::core::ManualExtractionState::NoExtractableText))) {
        // HUMAN-APPROVED (T027 §60.4): an explicit no-text state — never a
        // blank preview that would fake an ordinary extraction success.
        m_previewStateToken = QStringLiteral("no_extractable_text");
        return;
    }

    if (document.documentType
        == modbuslens::core::ManualDocumentType::Pdf) {
        bool ok = false;
        const QStringList pages = ManualStore::loadPdfPages(contentHash, &ok);
        if (ok) {
            m_previewText = buildPdfPreview(pages);
        }
        return;
    }

    bool ok = false;
    m_previewText = ManualStore::loadText(contentHash, &ok);
    if (!ok) {
        m_previewText.clear();
    }
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
    refreshPreview();
    emit selectionChanged();
    return true;
}

void ManualImportController::selectDocument(int index)
{
    if (index < 0 || index >= static_cast<int>(m_documents.size())) {
        m_selectedIndex = -1;
        refreshPreview();
        emit selectionChanged();
        return;
    }
    m_selectedIndex = index;
    refreshPreview();
    emit selectionChanged();
}

void ManualImportController::clearSelection()
{
    m_selectedIndex = -1;
    refreshPreview();
    emit selectionChanged();
}

void ManualImportController::refresh()
{
    m_documents = ManualStore::loadAll();
    if (m_selectedIndex >= static_cast<int>(m_documents.size())) {
        m_selectedIndex = -1;
    }
    refreshPreview();
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
    // A page number is NEVER fabricated: TXT / Markdown / DOCX have none.
    map[QStringLiteral("pageNumber")] = -1;
    map[QStringLiteral("section")] = QString();
    map[QStringLiteral("textStart")] = from;
    map[QStringLiteral("textEnd")] = to;
    map[QStringLiteral("excerpt")] = excerpt;
    return map;
}

} // namespace modbuslens::ui
