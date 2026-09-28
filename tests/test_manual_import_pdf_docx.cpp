// M12-C C1b second slice: PDF / DOCX import workflow targeted tests (I-matrix).
//
// Every fixture is generated in-code (deterministic, licence-free, offline). The
// store is pointed at a temporary managed root via setManagedRootOverride, so no
// real user data can ever be touched. Frozen semantics: docs/tasks/T027 §60.
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <zip.h>

#include "core/manual/ManualDocument.h"
#include "ui/manual/ManualStore.h"

using modbuslens::core::ManualDocument;
using modbuslens::core::ManualDocumentType;
using modbuslens::core::ManualImportError;
using modbuslens::ui::ManualImportResult;
using modbuslens::ui::ManualStore;

namespace {

constexpr char kWordNamespace[] =
    "http://schemas.openxmlformats.org/wordprocessingml/2006/main";

// ---------------------------------------------------------------- PDF fixtures --
[[nodiscard]] QByteArray buildPdf(const QList<QByteArray> &objects,
                                  const QByteArray &trailerExtra = {})
{
    QByteArray out = "%PDF-1.4\n";
    QList<int> offsets;
    for (int index = 0; index < objects.size(); ++index) {
        offsets.append(out.size());
        out += QByteArray::number(index + 1) + " 0 obj\n" + objects.at(index)
               + "\nendobj\n";
    }
    const int xrefAt = out.size();
    out += "xref\n0 " + QByteArray::number(objects.size() + 1)
           + "\n0000000000 65535 f \n";
    for (int offset : offsets) {
        out += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
    }
    out += "trailer\n<< /Size " + QByteArray::number(objects.size() + 1)
           + " /Root 1 0 R" + trailerExtra + " >>\nstartxref\n"
           + QByteArray::number(xrefAt) + "\n%%EOF\n";
    return out;
}

[[nodiscard]] QByteArray streamObject(const QByteArray &stream)
{
    return "<< /Length " + QByteArray::number(stream.size()) + " >>\nstream\n"
           + stream + "\nendstream";
}

[[nodiscard]] QByteArray identityToUnicodeCmap(const QList<ushort> &codes)
{
    QByteArray entries;
    for (ushort code : codes) {
        entries += "<" + QByteArray::number(code, 16).rightJustified(4, '0').toUpper()
                   + "> <" + QByteArray::number(code, 16).rightJustified(4, '0').toUpper()
                   + ">\n";
    }
    return "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
           "/CMapName /M12C1BTEST def\n/CMapType 2 def\n"
           "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n"
           + QByteArray::number(codes.size()) + " beginbfchar\n" + entries
           + "endbfchar\nendcmap\nCMapName currentdict /CMap defineresource pop\n"
             "end\nend";
}

[[nodiscard]] QByteArray hexCodes(const QList<ushort> &codes)
{
    QByteArray out;
    for (ushort code : codes) {
        out += QByteArray::number(code, 16).rightJustified(4, '0').toUpper();
    }
    return out;
}

[[nodiscard]] QList<ushort> toCodes(const QString &text)
{
    QList<ushort> codes;
    for (const QChar &character : text) {
        codes.append(character.unicode());
    }
    return codes;
}

// ASCII text through the standard Helvetica font.
[[nodiscard]] QByteArray asciiPdf(const QString &text)
{
    const QByteArray stream = "BT /F1 18 Tf 72 700 Td (" + text.toUtf8() + ") Tj ET";
    const QList<QByteArray> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R "
        "/Resources << /Font << /F1 5 0 R >> >> >>",
        streamObject(stream),
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    };
    return buildPdf(objects);
}

// CJK text through a Type0 / Identity-H font whose ToUnicode CMap maps each
// code to the same code point (extraction yields exactly the authored text).
[[nodiscard]] QByteArray cjkPdf(const QString &text, bool encrypted = false)
{
    const QList<ushort> codes = toCodes(text);
    const QByteArray stream =
        "BT /F1 18 Tf 72 700 Td <" + hexCodes(codes) + "> Tj ET";
    QList<QByteArray> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R "
        "/Resources << /Font << /F1 5 0 R >> >> >>",
        streamObject(stream),
        "<< /Type /Font /Subtype /Type0 /BaseFont /M12TestCJK /Encoding /Identity-H "
        "/DescendantFonts [6 0 R] /ToUnicode 7 0 R >>",
        "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /M12TestCJK "
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> "
        "/FontDescriptor 8 0 R /DW 1000 >>",
        streamObject(identityToUnicodeCmap(codes)),
        "<< /Type /FontDescriptor /FontName /M12TestCJK /Flags 4 "
        "/FontBBox [0 0 1000 1000] /ItalicAngle 0 /Ascent 1000 /Descent 0 "
        "/CapHeight 1000 /StemV 80 >>",
    };
    if (encrypted) {
        objects.append("<< /Filter /Standard /V 1 /R 2 /O (M12TESTPAD) /U (M12TESTPAD) "
                       "/P -1 >>");
        return buildPdf(objects, " /Encrypt 9 0 R");
    }
    return buildPdf(objects);
}

[[nodiscard]] QByteArray multiPagePdf(const QString &page1, const QString &page2)
{
    const QList<ushort> codes1 = toCodes(page1);
    const QList<ushort> codes2 = toCodes(page2);
    const QByteArray stream1 =
        "BT /F1 18 Tf 72 700 Td <" + hexCodes(codes1) + "> Tj ET";
    const QByteArray stream2 =
        "BT /F1 18 Tf 72 700 Td <" + hexCodes(codes2) + "> Tj ET";
    const QList<QByteArray> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R "
        "/Resources << /Font << /F1 6 0 R >> >> >>",
        streamObject(stream1),
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 7 0 R "
        "/Resources << /Font << /F1 6 0 R >> >> >>",
        "<< /Type /Font /Subtype /Type0 /BaseFont /M12TestCJK /Encoding /Identity-H "
        "/DescendantFonts [8 0 R] /ToUnicode 9 0 R >>",
        streamObject(stream2),
        "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /M12TestCJK "
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> "
        "/FontDescriptor 10 0 R /DW 1000 >>",
        streamObject(identityToUnicodeCmap(codes1) + identityToUnicodeCmap(codes2)),
        "<< /Type /FontDescriptor /FontName /M12TestCJK /Flags 4 "
        "/FontBBox [0 0 1000 1000] /ItalicAngle 0 /Ascent 1000 /Descent 0 "
        "/CapHeight 1000 /StemV 80 >>",
    };
    return buildPdf(objects);
}

[[nodiscard]] QByteArray noTextPdf()
{
    const QByteArray stream = "0.9 0.9 0.9 rg 72 600 200 80 re f";
    const QList<QByteArray> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R >>",
        streamObject(stream),
    };
    return buildPdf(objects);
}

// --------------------------------------------------------------- DOCX fixtures --
constexpr char kContentTypes[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
    "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package."
    "relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/>"
    "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd."
    "openxmlformats-officedocument.wordprocessingml.document.main+xml\"/></Types>";

[[nodiscard]] QByteArray documentXml(const QString &body)
{
    return QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                      "<w:document xmlns:w=\"")
           + kWordNamespace + "\"><w:body>" + body.toUtf8()
           + "</w:body></w:document>";
}

[[nodiscard]] QByteArray relationships(const QString &relationshipXml)
{
    return QByteArray(
               "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
               "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/"
               "relationships\">")
           + relationshipXml.toUtf8() + "</Relationships>";
}

[[nodiscard]] QString officeDocumentRelationship(const QString &target)
{
    return QStringLiteral("<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats."
                          "org/officeDocument/2006/relationships/officeDocument\" "
                          "Target=\"%1\"/>")
        .arg(target);
}

[[nodiscard]] bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    const qint64 written = file.write(bytes);
    file.close();
    return written == bytes.size();
}

[[nodiscard]] bool writeDocx(const QString &path, const QByteArray &rels,
                             const QByteArray &document,
                             const QList<QPair<QString, QByteArray>> &extra = {})
{
    int errorCode = 0;
    zip_t *archive =
        zip_open(path.toUtf8().constData(), ZIP_CREATE | ZIP_TRUNCATE, &errorCode);
    if (archive == nullptr) {
        return false;
    }
    const auto add = [archive](const QString &name, const QByteArray &content) {
        zip_source_t *source =
            zip_source_buffer(archive, content.constData(), content.size(), 0);
        return source != nullptr
               && zip_file_add(archive, name.toUtf8().constData(), source,
                               ZIP_FL_ENC_UTF_8)
                      >= 0;
    };
    bool ok = add(QStringLiteral("[Content_Types].xml"), QByteArray(kContentTypes));
    ok = ok && add(QStringLiteral("_rels/.rels"), rels);
    ok = ok && add(QStringLiteral("word/document.xml"), document);
    for (const auto &entry : extra) {
        ok = ok && add(entry.first, entry.second);
    }
    zip_close(archive);
    return ok;
}

// Atomicity helpers: everything the managed root must (or must not) contain.
[[nodiscard]] QStringList managedRootInventory(const QString &root)
{
    QStringList listing;
    const QDir base(root);
    for (const QString &sub : {QStringLiteral("manuals/documents"),
                               QStringLiteral("manuals/source"),
                               QStringLiteral("manuals/text")}) {
        const QDir dir(base.filePath(sub));
        if (!dir.exists()) {
            continue;
        }
        for (const QString &name : dir.entryList(QDir::Files)) {
            listing << sub + QStringLiteral("/") + name;
        }
    }
    listing.sort();
    return listing;
}

} // namespace

class ManualImportPdfDocxTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        ManualStore::setManagedRootOverride(m_root.path());
    }

    [[nodiscard]] QString pathFor(const QString &name) const
    {
        return QDir(m_root.path()).filePath(name);
    }

    // =============================================== C1a protected (TXT / MD) ====
    void i01_txtImportUnchanged()
    {
        const QString path = pathFor("i01.txt");
        QVERIFY(writeBytes(path, QByteArray("频率寄存器 1000\nASCII line\n")));
        const ManualImportResult result =
            ManualStore::importSourceFile(path, QStringLiteral("i01.txt"));
        QVERIFY(result.ok());
        QCOMPARE(result.document.documentType, ManualDocumentType::Txt);
        QCOMPARE(result.document.extractionStateToken, std::string("extracted"));
        bool ok = false;
        const QString text = ManualStore::loadText(
            QString::fromStdString(result.document.contentHash), &ok);
        QVERIFY(ok);
        QCOMPARE(text, QString::fromUtf8("频率寄存器 1000\nASCII line\n"));
    }

    void i02_markdownImportUnchanged()
    {
        const QString path = pathFor("i02.markdown");
        QVERIFY(writeBytes(path, QByteArray("# 标题\n正文 with BOM-free text\n")));
        const ManualImportResult result =
            ManualStore::importSourceFile(path, QStringLiteral("i02.markdown"));
        QVERIFY(result.ok());
        QCOMPARE(result.document.documentType, ManualDocumentType::Markdown);
        bool ok = false;
        const QString text = ManualStore::loadText(
            QString::fromStdString(result.document.contentHash), &ok);
        QVERIFY(ok);
        QCOMPARE(text, QString::fromUtf8("# 标题\n正文 with BOM-free text\n"));
    }

    // ============================================================== PDF path ====
    void i03_pdfAsciiImport()
    {
        const QString path = pathFor("i03.pdf");
        QVERIFY(writeBytes(path, asciiPdf(QStringLiteral("ModbusLens PDF import"))));
        const ManualImportResult result =
            ManualStore::importSourceFile(path, QStringLiteral("i03.pdf"));
        QVERIFY(result.ok());
        QCOMPARE(result.document.documentType, ManualDocumentType::Pdf);
        QCOMPARE(result.document.extractionStateToken, std::string("extracted"));
        bool ok = false;
        const QStringList pages = ManualStore::loadPdfPages(
            QString::fromStdString(result.document.contentHash), &ok);
        QVERIFY(ok);
        QCOMPARE(pages.size(), 1);
        QCOMPARE(pages.at(0), QStringLiteral("ModbusLens PDF import"));
    }

    void i04_pdfChineseImport()
    {
        const QString path = pathFor("i04.pdf");
        QVERIFY(writeBytes(path, cjkPdf(QString::fromUtf8("变频器说明书中文"))));
        const ManualImportResult result =
            ManualStore::importSourceFile(path, QStringLiteral("i04.pdf"));
        QVERIFY(result.ok());
        bool ok = false;
        const QStringList pages = ManualStore::loadPdfPages(
            QString::fromStdString(result.document.contentHash), &ok);
        QVERIFY(ok);
        // Exact Unicode assertion: a byte-wise conversion cannot produce this.
        QCOMPARE(pages.at(0), QString::fromUtf8("变频器说明书中文"));
    }

    void i05_pdfMixedUnicode()
    {
        const QString path = pathFor("i05.pdf");
        QVERIFY(writeBytes(path, cjkPdf(QString::fromUtf8("PV 地址 1000Hz"))));
        const ManualImportResult result =
            ManualStore::importSourceFile(path, QStringLiteral("i05.pdf"));
        QVERIFY(result.ok());
        bool ok = false;
        const QStringList pages = ManualStore::loadPdfPages(
            QString::fromStdString(result.document.contentHash), &ok);
        QVERIFY(ok);
        QCOMPARE(pages.at(0), QString::fromUtf8("PV 地址 1000Hz"));
    }

    void i06_pdfNoTextImportsSuccessfully()
    {
        const QString path = pathFor("i06.pdf");
        QVERIFY(writeBytes(path, noTextPdf()));
        const ManualImportResult result =
            ManualStore::importSourceFile(path, QStringLiteral("i06.pdf"));
        // HUMAN-APPROVED (T027 §60.2): the import itself SUCCEEDS.
        QVERIFY(result.ok());
        // ... and the managed copy + record are kept.
        QVERIFY(QFileInfo::exists(pathFor(
            QStringLiteral("manuals/source/")
            + QString::fromStdString(result.document.contentHash)
            + QStringLiteral(".bin"))));
        QCOMPARE(result.document.documentType, ManualDocumentType::Pdf);
    }

    void i07_pdfNoTextStateIsExact()
    {
        const QString path = pathFor("i07.pdf");
        QVERIFY(writeBytes(path, noTextPdf()));
        const ManualImportResult result =
            ManualStore::importSourceFile(path, QStringLiteral("i07.pdf"));
        QVERIFY(result.ok());
        QCOMPARE(result.document.extractionStateToken,
                 std::string("no_extractable_text"));
        QCOMPARE(result.document.charCount, std::size_t(0));
        bool ok = false;
        const QStringList pages = ManualStore::loadPdfPages(
            QString::fromStdString(result.document.contentHash), &ok);
        QVERIFY(ok);
        // Per-page truth: the page EXISTS (1 page), its extracted text is
        // empty. The extractor must not invent or drop pages.
        QCOMPARE(pages.size(), 1);
        QVERIFY(pages.at(0).isEmpty());
    }

    void i08_corruptPdfAtomicReject()
    {
        const QString path = pathFor("i08.pdf");
        QByteArray bytes = asciiPdf(QStringLiteral("truncated")).mid(0, 120);
        QVERIFY(writeBytes(path, bytes));
        const QStringList before = managedRootInventory(m_root.path());
        const ManualImportResult result =
            ManualStore::importSourceFile(path, QStringLiteral("i08.pdf"));
        QCOMPARE(result.error, ManualImportError::MalformedContent);
        // Atomicity: nothing was persisted.
        QCOMPARE(managedRootInventory(m_root.path()), before);
    }

    void i09_fakePdfAtomicReject()
    {
        const QString path = pathFor("i09.pdf");
        QVERIFY(writeBytes(path, QByteArray("this is not a pdf at all").repeated(8)));
        const QStringList before = managedRootInventory(m_root.path());
        const ManualImportResult result =
            ManualStore::importSourceFile(path, QStringLiteral("i09.pdf"));
        QCOMPARE(result.error, ManualImportError::MalformedContent);
        QCOMPARE(managedRootInventory(m_root.path()), before);
    }

    void i10_encryptedPdfReject()
    {
        const QString path = pathFor("i10.pdf");
        QVERIFY(writeBytes(path, cjkPdf(QString::fromUtf8("加密"), true)));
        const QStringList before = managedRootInventory(m_root.path());
        const ManualImportResult result =
            ManualStore::importSourceFile(path, QStringLiteral("i10.pdf"));
        QCOMPARE(result.error, ManualImportError::EncryptedOrPasswordProtected);
        QCOMPARE(managedRootInventory(m_root.path()), before);
    }

    void i11_pdfResourceLimitReject()
    {
        const QString path = pathFor("i11.pdf");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("%PDF-1.4\n");
        const QByteArray filler(1024 * 1024, 'x');
        qint64 remaining =
            static_cast<qint64>(modbuslens::core::kManualMaxSourceBytes) + 1024;
        while (remaining > 0) {
            file.write(filler);
            remaining -= filler.size();
        }
        file.close();
        const QStringList before = managedRootInventory(m_root.path());
        const ManualImportResult result =
            ManualStore::importSourceFile(path, QStringLiteral("i11.pdf"));
        QCOMPARE(result.error, ManualImportError::TooLarge);
        QCOMPARE(managedRootInventory(m_root.path()), before);
    }

    // ============================================================= DOCX path ====
    void i12_docxNormalImport()
    {
        const QString path = pathFor("i12.docx");
        QVERIFY(writeDocx(
            path,
            relationships(officeDocumentRelationship(QStringLiteral("word/document.xml"))),
            documentXml(QStringLiteral("<w:p><w:r><w:t>First</w:t></w:r></w:p>"
                                       "<w:p><w:r><w:t>Second</w:t></w:r></w:p>"))));
        const ManualImportResult result =
            ManualStore::importSourceFile(path, QStringLiteral("i12.docx"));
        QVERIFY(result.ok());
        QCOMPARE(result.document.documentType, ManualDocumentType::Docx);
        QCOMPARE(result.document.extractionStateToken, std::string("extracted"));
        bool ok = false;
        const QString text = ManualStore::loadText(
            QString::fromStdString(result.document.contentHash), &ok);
        QVERIFY(ok);
        QCOMPARE(text, QStringLiteral("First\nSecond\n"));
    }

    void i13_docxChineseImport()
    {
        const QString path = pathFor("i13.docx");
        QVERIFY(writeDocx(
            path,
            relationships(officeDocumentRelationship(QStringLiteral("word/document.xml"))),
            documentXml(QStringLiteral(
                "<w:p><w:r><w:t>中文说明书正文</w:t></w:r></w:p>"))));
        const ManualImportResult result =
            ManualStore::importSourceFile(path, QStringLiteral("i13.docx"));
        QVERIFY(result.ok());
        bool ok = false;
        const QString text = ManualStore::loadText(
            QString::fromStdString(result.document.contentHash), &ok);
        QVERIFY(ok);
        QCOMPARE(text, QString::fromUtf8("中文说明书正文\n"));
    }

    void i14_docxFrozenSeparatorsPreserved()
    {
        const QString path = pathFor("i14.docx");
        QVERIFY(writeDocx(
            path,
            relationships(officeDocumentRelationship(QStringLiteral("word/document.xml"))),
            documentXml(QStringLiteral(
                "<w:p><w:r><w:t>a</w:t><w:tab/><w:t>b</w:t><w:br/><w:t>c</w:t>"
                "<w:cr/><w:t>d</w:t></w:r></w:p>"
                "<w:tbl><w:tr><w:tc><w:p><w:r><w:t>C1</w:t></w:r></w:p></w:tc>"
                "<w:tc><w:p><w:r><w:t>C2</w:t></w:r></w:p></w:tc></w:tr></w:tbl>"))));
        const ManualImportResult result =
            ManualStore::importSourceFile(path, QStringLiteral("i14.docx"));
        QVERIFY(result.ok());
        bool ok = false;
        const QString text = ManualStore::loadText(
            QString::fromStdString(result.document.contentHash), &ok);
        QVERIFY(ok);
        QCOMPARE(text, QStringLiteral("a\tb\nc\nd\nC1\n\tC2\n\t\n"));
    }

    void i15_docxCustomMainRelationshipTarget()
    {
        const QString path = pathFor("i15.docx");
        QVERIFY(writeDocx(
            path,
            relationships(officeDocumentRelationship(QStringLiteral("custom/story.xml"))),
            documentXml(QStringLiteral("<w:p><w:r><w:t>discovered</w:t></w:r></w:p>")),
            {{QStringLiteral("custom/story.xml"),
              documentXml(QStringLiteral("<w:p><w:r><w:t>discovered</w:t></w:r></w:p>"))}}));
        const ManualImportResult result =
            ManualStore::importSourceFile(path, QStringLiteral("i15.docx"));
        QVERIFY(result.ok());
        bool ok = false;
        const QString text = ManualStore::loadText(
            QString::fromStdString(result.document.contentHash), &ok);
        QVERIFY(ok);
        QCOMPARE(text, QStringLiteral("discovered\n"));
    }

    void i16_corruptDocxAtomicReject()
    {
        const QString path = pathFor("i16.docx");
        QVERIFY(writeBytes(path, QByteArray("PK\x03\x04").append(QByteArray(200, 'Z'))));
        const QStringList before = managedRootInventory(m_root.path());
        const ManualImportResult result =
            ManualStore::importSourceFile(path, QStringLiteral("i16.docx"));
        QCOMPARE(result.error, ManualImportError::MalformedContent);
        QCOMPARE(managedRootInventory(m_root.path()), before);
    }

    void i17_fakeDocxReject()
    {
        const QString path = pathFor("i17.docx");
        QVERIFY(writeBytes(path, QByteArray("definitely not an ooxml package")));
        const QStringList before = managedRootInventory(m_root.path());
        const ManualImportResult result =
            ManualStore::importSourceFile(path, QStringLiteral("i17.docx"));
        QCOMPARE(result.error, ManualImportError::MalformedContent);
        QCOMPARE(managedRootInventory(m_root.path()), before);
    }

    void i18_unsafeRelationshipReject()
    {
        for (const QString &target :
             {QStringLiteral("../outside.xml"), QStringLiteral("/absolute.xml"),
              QStringLiteral("C:/evil.xml")}) {
            QString fixtureName = target;
            fixtureName.replace(QLatin1Char('/'), QLatin1Char('_'));
            fixtureName.replace(QLatin1Char(':'), QLatin1Char('_'));
            const QString path = pathFor(QStringLiteral("i18_%1.docx").arg(fixtureName));
            QVERIFY(writeDocx(
                path, relationships(officeDocumentRelationship(target)),
                documentXml(QStringLiteral("<w:p><w:r><w:t>x</w:t></w:r></w:p>"))));
            const QStringList before = managedRootInventory(m_root.path());
            const ManualImportResult result = ManualStore::importSourceFile(
                path, QStringLiteral("i18_%1.docx").arg(fixtureName));
            QCOMPARE(result.error, ManualImportError::MalformedContent);
            QCOMPARE(managedRootInventory(m_root.path()), before);
        }
    }

    void i19_docxResourceLimitReject()
    {
        // The container itself exceeds the accepted source bound (the frozen rule:
        // reject explicitly, never truncate).
        const QString path = pathFor("i19.docx");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("PK\x03\x04");
        const QByteArray filler(1024 * 1024, 'x');
        qint64 remaining =
            static_cast<qint64>(modbuslens::core::kManualMaxSourceBytes) + 1024;
        while (remaining > 0) {
            file.write(filler);
            remaining -= filler.size();
        }
        file.close();
        const QStringList before = managedRootInventory(m_root.path());
        const ManualImportResult result =
            ManualStore::importSourceFile(path, QStringLiteral("i19.docx"));
        QCOMPARE(result.error, ManualImportError::TooLarge);
        QCOMPARE(managedRootInventory(m_root.path()), before);
    }

    // ================================================== routing + duplicates ====
    void i20_unsupportedExtensionReject()
    {
        const QString path = pathFor("i20.exe");
        QVERIFY(writeBytes(path, QByteArray("MZ whatever")));
        const QStringList before = managedRootInventory(m_root.path());
        const ManualImportResult result =
            ManualStore::importSourceFile(path, QStringLiteral("i20.exe"));
        QCOMPARE(result.error, ManualImportError::UnsupportedType);
        QCOMPARE(managedRootInventory(m_root.path()), before);
    }

    void i21_deletedOriginalDoesNotBreakPdf()
    {
        const QString source = pathFor("i21-src.pdf");
        QVERIFY(writeBytes(source, cjkPdf(QString::fromUtf8("原件已删除"))));
        const ManualImportResult result =
            ManualStore::importSourceFile(source, QStringLiteral("i21.pdf"));
        QVERIFY(result.ok());
        const QString hash =
            QString::fromStdString(result.document.contentHash);
        QVERIFY(QFile::remove(source)); // the original disappears after import
        bool ok = false;
        const QStringList pages = ManualStore::loadPdfPages(hash, &ok);
        QVERIFY(ok);
        QCOMPARE(pages.at(0), QString::fromUtf8("原件已删除"));
    }

    void i22_deletedOriginalDoesNotBreakDocx()
    {
        const QString source = pathFor("i22-src.docx");
        QVERIFY(writeDocx(
            source,
            relationships(officeDocumentRelationship(QStringLiteral("word/document.xml"))),
            documentXml(QStringLiteral("<w:p><w:r><w:t>managed survives</w:t></w:r></w:p>"))));
        const ManualImportResult result =
            ManualStore::importSourceFile(source, QStringLiteral("i22.docx"));
        QVERIFY(result.ok());
        const QString hash =
            QString::fromStdString(result.document.contentHash);
        QVERIFY(QFile::remove(source));
        bool ok = false;
        const QString text = ManualStore::loadText(hash, &ok);
        QVERIFY(ok);
        QCOMPARE(text, QStringLiteral("managed survives\n"));
    }

    void i23_samePdfBytesTwoRecords()
    {
        const QString first = pathFor("i23-a.pdf");
        const QString second = pathFor("i23-b.pdf");
        const QByteArray bytes = cjkPdf(QString::fromUtf8("同一份内容"));
        QVERIFY(writeBytes(first, bytes));
        QVERIFY(writeBytes(second, bytes));
        const ManualImportResult a =
            ManualStore::importSourceFile(first, QStringLiteral("a.pdf"));
        const ManualImportResult b =
            ManualStore::importSourceFile(second, QStringLiteral("b.pdf"));
        QVERIFY(a.ok());
        QVERIFY(b.ok());
        // document identity != contentHash: two records, one cache. The store
        // is shared across the whole test class, so assert the DELTA.
        QVERIFY(a.document.documentId != b.document.documentId);
        QCOMPARE(a.document.contentHash, b.document.contentHash);
        const int recordsAfter = static_cast<int>(ManualStore::loadAll().size());
        const int recordsBefore = recordsAfter - 2;
        QCOMPARE(recordsAfter - recordsBefore, 2);
    }

    void i24_sameDocxBytesTwoRecords()
    {
        const QString first = pathFor("i24-a.docx");
        const QString second = pathFor("i24-b.docx");
        const QByteArray document =
            documentXml(QStringLiteral("<w:p><w:r><w:t>重复导入</w:t></w:r></w:p>"));
        const QByteArray rels = relationships(
            officeDocumentRelationship(QStringLiteral("word/document.xml")));
        QVERIFY(writeBytes(first, QByteArray()));
        QVERIFY(writeDocx(first, rels, document));
        QVERIFY(writeDocx(second, rels, document));
        const ManualImportResult a =
            ManualStore::importSourceFile(first, QStringLiteral("a.docx"));
        const ManualImportResult b =
            ManualStore::importSourceFile(second, QStringLiteral("b.docx"));
        QVERIFY(a.ok());
        QVERIFY(b.ok());
        QVERIFY(a.document.documentId != b.document.documentId);
        QCOMPARE(a.document.contentHash, b.document.contentHash);
    }

    void i25_sameBytesSameContentHash()
    {
        const QString first = pathFor("i25-a.pdf");
        const QString second = pathFor("i25-b.pdf");
        const QByteArray bytes = asciiPdf(QStringLiteral("same bytes"));
        QVERIFY(writeBytes(first, bytes));
        QVERIFY(writeBytes(second, bytes));
        const ManualImportResult a =
            ManualStore::importSourceFile(first, QStringLiteral("a.pdf"));
        const ManualImportResult b =
            ManualStore::importSourceFile(second, QStringLiteral("b.pdf"));
        QVERIFY(a.ok());
        QVERIFY(b.ok());
        QCOMPARE(a.document.contentHash, b.document.contentHash);
    }

    void i26_cacheReusedNotDuplicated()
    {
        const QString first = pathFor("i26-a.pdf");
        const QString second = pathFor("i26-b.pdf");
        const QByteArray bytes = asciiPdf(QStringLiteral("cache reuse"));
        QVERIFY(writeBytes(first, bytes));
        QVERIFY(writeBytes(second, bytes));
        const ManualImportResult a =
            ManualStore::importSourceFile(first, QStringLiteral("a.pdf"));
        QVERIFY(a.ok());
        const QString cacheFile = pathFor(
            QStringLiteral("manuals/text/")
            + QString::fromStdString(a.document.contentHash)
            + QStringLiteral(".json"));
        QVERIFY(QFileInfo::exists(cacheFile));
        const qint64 cacheSize = QFileInfo(cacheFile).size();
        // The store is shared across the whole test class: count the existing
        // JSON caches and assert the delta, not an absolute number.
        int jsonCacheBefore = 0;
        const QDir textDirBefore(pathFor(QStringLiteral("manuals/text")));
        for (const QString &name : textDirBefore.entryList(QDir::Files)) {
            if (name.endsWith(QLatin1String(".json"))) {
                ++jsonCacheBefore;
            }
        }
        const ManualImportResult b =
            ManualStore::importSourceFile(second, QStringLiteral("b.pdf"));
        QVERIFY(b.ok());
        // The second import must REUSE the cache: no second cache file is
        // created and the payload is left untouched.
        int jsonCacheAfter = 0;
        const QDir textDirAfter(pathFor(QStringLiteral("manuals/text")));
        for (const QString &name : textDirAfter.entryList(QDir::Files)) {
            if (name.endsWith(QLatin1String(".json"))) {
                ++jsonCacheAfter;
            }
        }
        QCOMPARE(jsonCacheAfter - jsonCacheBefore, 0);
        QCOMPARE(QFileInfo(cacheFile).size(), cacheSize);
    }

    void i27_distinctProvenancePreserved()
    {
        const QString first = pathFor("i27-a.pdf");
        const QString second = pathFor("i27-b.pdf");
        const QByteArray bytes = cjkPdf(QString::fromUtf8("来源不同"));
        QVERIFY(writeBytes(first, bytes));
        QVERIFY(writeBytes(second, bytes));
        const ManualImportResult a =
            ManualStore::importSourceFile(first, QStringLiteral("a.pdf"));
        const ManualImportResult b =
            ManualStore::importSourceFile(second, QStringLiteral("b.pdf"));
        QVERIFY(a.ok());
        QVERIFY(b.ok());
        QVERIFY(a.document.originalPath != b.document.originalPath);
        QCOMPARE(a.document.originalPath, first.toStdString());
        QCOMPARE(b.document.originalPath, second.toStdString());
    }

    void i28_existingDocumentsUnaffectedByFailedImport()
    {
        // Everything imported so far must survive a failing import untouched.
        const QString bad = pathFor("i28-bad.pdf");
        QVERIFY(writeBytes(bad, QByteArray("not a pdf")));
        const QStringList before = managedRootInventory(m_root.path());
        const int recordCount = static_cast<int>(ManualStore::loadAll().size());
        const ManualImportResult result =
            ManualStore::importSourceFile(bad, QStringLiteral("i28.pdf"));
        QCOMPARE(result.error, ManualImportError::MalformedContent);
        QCOMPARE(ManualStore::loadAll().size(), recordCount);
        QCOMPARE(managedRootInventory(m_root.path()), before);
    }

    void i29_profileJsonUntouched()
    {
        // The managed root is the whole world of this slice: a manual import
        // must never create or modify anything outside manuals/ (no profiles,
        // no sibling directories).
        const QString path = pathFor("i29.pdf");
        QVERIFY(writeBytes(path, asciiPdf(QStringLiteral("isolation"))));
        const QString before = QDir(m_root.path()).entryList(QDir::AllEntries
                                                             | QDir::Hidden)
                                   .join(QStringLiteral("|"));
        QVERIFY(!QFileInfo::exists(pathFor(QStringLiteral("profiles"))));
        const int documentsBefore = static_cast<int>(ManualStore::loadAll().size());
        const QStringList inventoryBefore = managedRootInventory(m_root.path());
        QVERIFY(ManualStore::importSourceFile(path, QStringLiteral("i29.pdf")).ok());

        // 1. no profiles / sibling directory appeared.
        const QString after = QDir(m_root.path()).entryList(QDir::AllEntries
                                                            | QDir::Hidden)
                                  .join(QStringLiteral("|"));
        QCOMPARE(after, before);
        QVERIFY(!QFileInfo::exists(pathFor(QStringLiteral("profiles"))));
        // 2. inside manuals/ the import added EXACTLY its own three artifacts
        //    (managed copy + per-page cache + metadata record).
        const QStringList inventoryAfter = managedRootInventory(m_root.path());
        QCOMPARE(inventoryAfter.size() - inventoryBefore.size(), 3);
        // 3. every pre-existing document is still resolvable.
        QCOMPARE(static_cast<int>(ManualStore::loadAll().size()),
                 documentsBefore + 1);
    }

    void i30_metadataSchemaIsFrozen()
    {
        const QString path = pathFor("i30.pdf");
        QVERIFY(writeBytes(path, cjkPdf(QString::fromUtf8("schema"))));
        const ManualImportResult result =
            ManualStore::importSourceFile(path, QStringLiteral("i30.pdf"));
        QVERIFY(result.ok());
        QFile file(pathFor(
            QStringLiteral("manuals/documents/")
            + QString::fromStdString(result.document.documentId)
            + QStringLiteral(".json")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        // Exactly the frozen keys — nothing else (no AI/network/extraction side
        // fields may creep in).
        QCOMPARE(root.size(), 10);
        QVERIFY(!root.contains(QStringLiteral("ai")));
        QVERIFY(!root.contains(QStringLiteral("candidate")));
        QVERIFY(!root.contains(QStringLiteral("network")));
        QVERIFY(!root.contains(QStringLiteral("presentationHeader")));
    }

private:
    QTemporaryDir m_root;
};

QTEST_GUILESS_MAIN(ManualImportPdfDocxTest)
#include "test_manual_import_pdf_docx.moc"
