// M12-C C1b targeted tests: deterministic PDF and DOCX text extraction.
//
// All fixtures are generated in-code into a temporary directory: no downloaded or
// third-party document is used, there is no network access and no Office/LibreOffice
// dependency. Expectations come from the frozen semantics in docs/tasks/T027 section 58.
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <zip.h>

#include "ui/manual/ManualDocxTextExtractor.h"
#include "ui/manual/ManualPdfTextExtractor.h"
#include "ui/manual/ManualTextExtraction.h"

using modbuslens::ui::ManualDocxTextExtractor;
using modbuslens::ui::ManualExtractionResult;
using modbuslens::ui::ManualExtractionStatus;
using modbuslens::ui::ManualPdfTextExtractor;

namespace {

constexpr char kWordNamespace[] =
    "http://schemas.openxmlformats.org/wordprocessingml/2006/main";

// ---------------------------------------------------------------- PDF fixtures --
[[nodiscard]] QByteArray buildPdf(const QList<QByteArray> &objects)
{
    QByteArray out = "%PDF-1.4\n";
    QList<int> offsets;
    for (int index = 0; index < objects.size(); ++index) {
        offsets.append(out.size());
        out += QByteArray::number(index + 1) + " 0 obj\n" + objects.at(index) + "\nendobj\n";
    }
    const int xrefAt = out.size();
    out += "xref\n0 " + QByteArray::number(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (int offset : offsets) {
        out += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
    }
    out += "trailer\n<< /Size " + QByteArray::number(objects.size() + 1)
           + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xrefAt) + "\n%%EOF\n";
    return out;
}

[[nodiscard]] QByteArray streamObject(const QByteArray &stream)
{
    return "<< /Length " + QByteArray::number(stream.size()) + " >>\nstream\n" + stream
           + "\nendstream";
}

// Type0 / Identity-H font whose ToUnicode CMap maps each code to the same code point, so
// extraction is expected to yield exactly the authored characters.
[[nodiscard]] QByteArray identityToUnicodeCmap(const QList<ushort> &codes)
{
    QByteArray entries;
    for (ushort code : codes) {
        entries += "<" + QByteArray::number(code, 16).rightJustified(4, '0').toUpper()
                   + "> <" + QByteArray::number(code, 16).rightJustified(4, '0').toUpper()
                   + ">\n";
    }
    QByteArray cmap = "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
                      "/CMapName /M12C1BTEST def\n/CMapType 2 def\n"
                      "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n"
                      + QByteArray::number(codes.size()) + " beginbfchar\n" + entries
                      + "endbfchar\nendcmap\nCMapName currentdict /CMap defineresource pop\n"
                        "end\nend";
    return cmap;
}

[[nodiscard]] QByteArray hexCodes(const QList<ushort> &codes)
{
    QByteArray out;
    for (ushort code : codes) {
        out += QByteArray::number(code, 16).rightJustified(4, '0').toUpper();
    }
    return out;
}

struct PdfFixture {
    QByteArray bytes;
    QString expectedText;
};

[[nodiscard]] PdfFixture asciiPdf(const QString &text)
{
    const QByteArray stream =
        "BT /F1 18 Tf 72 700 Td (" + text.toUtf8() + ") Tj ET";
    const QList<QByteArray> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R "
        "/Resources << /Font << /F1 5 0 R >> >> >>",
        streamObject(stream),
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    };
    return {buildPdf(objects), text};
}

[[nodiscard]] PdfFixture cjkPdf(const QString &text)
{
    QList<ushort> codes;
    for (const QChar &character : text) {
        codes.append(character.unicode());
    }
    const QByteArray stream =
        "BT /F1 18 Tf 72 700 Td <" + hexCodes(codes) + "> Tj ET";
    const QList<QByteArray> objects = {
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
        "<< /Type /FontDescriptor /FontName /M12TestCJK /Flags 4 /FontBBox [0 0 1000 1000] "
        "/ItalicAngle 0 /Ascent 1000 /Descent 0 /CapHeight 1000 /StemV 80 >>",
    };
    return {buildPdf(objects), text};
}

[[nodiscard]] PdfFixture mixedPdf(const QString &ascii, const QString &cjk)
{
    QList<ushort> codes;
    for (const QChar &character : cjk) {
        codes.append(character.unicode());
    }
    const QByteArray stream = "BT /F1 18 Tf 72 700 Td (" + ascii.toUtf8() + ") Tj /F2 18 Tf <"
                              + hexCodes(codes) + "> Tj ET";
    const QList<QByteArray> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R "
        "/Resources << /Font << /F1 5 0 R /F2 6 0 R >> >> >>",
        streamObject(stream),
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        "<< /Type /Font /Subtype /Type0 /BaseFont /M12TestCJK /Encoding /Identity-H "
        "/DescendantFonts [7 0 R] /ToUnicode 8 0 R >>",
        "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /M12TestCJK "
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> "
        "/FontDescriptor 9 0 R /DW 1000 >>",
        streamObject(identityToUnicodeCmap(codes)),
        "<< /Type /FontDescriptor /FontName /M12TestCJK /Flags 4 /FontBBox [0 0 1000 1000] "
        "/ItalicAngle 0 /Ascent 1000 /Descent 0 /CapHeight 1000 /StemV 80 >>",
    };
    return {buildPdf(objects), ascii + cjk};
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

[[nodiscard]] bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    const qint64 written = file.write(bytes);
    file.close();
    return written == bytes.size();
}

// --------------------------------------------------------------- DOCX fixtures --
constexpr char kContentTypes[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
    "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package."
    "relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/>"
    "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd."
    "openxmlformats-officedocument.wordprocessingml.document.main+xml\"/></Types>";

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
    return QStringLiteral("<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/"
                          "officeDocument/2006/relationships/officeDocument\" Target=\"%1\"/>")
        .arg(target);
}

[[nodiscard]] QByteArray documentXml(const QString &bodyContent)
{
    return QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                      "<w:document xmlns:w=\"")
           + kWordNamespace + "\"><w:body>" + bodyContent.toUtf8()
           + "</w:body></w:document>";
}

[[nodiscard]] bool writeDocx(const QString &path, const QByteArray &rels,
                             const QByteArray &document, bool includeDocument,
                             const QList<QPair<QString, QByteArray>> &extra = {})
{
    int errorCode = 0;
    zip_t *archive = zip_open(path.toUtf8().constData(), ZIP_CREATE | ZIP_TRUNCATE,
                              &errorCode);
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
    if (!rels.isNull()) {
        ok = ok && add(QStringLiteral("_rels/.rels"), rels);
    }
    if (includeDocument) {
        ok = ok && add(QStringLiteral("word/document.xml"), document);
    }
    for (const auto &entry : extra) {
        ok = ok && add(entry.first, entry.second);
    }
    zip_close(archive);
    return ok;
}

} // namespace

class ManualExtractionTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
    }

    // =================================================================== PDF ====
    void pdf01_asciiTextLayer()
    {
        const PdfFixture fixture = asciiPdf(QStringLiteral("ModbusLens C1b ASCII text layer"));
        const QString path = QDir(m_root.path()).filePath("pdf01.pdf");
        QVERIFY(writeFile(path, fixture.bytes));
        const ManualExtractionResult result = ManualPdfTextExtractor::extractFromFile(path);
        QCOMPARE(modbuslens::ui::manualExtractionStatusToken(result.status), "ok");
        QCOMPARE(result.pageCount, 1);
        QCOMPARE(result.pageTexts.size(), 1);
        QCOMPARE(result.pageTexts.at(0), fixture.expectedText);
    }

    void pdf02_chineseTextLayer()
    {
        const PdfFixture fixture = cjkPdf(QString::fromUtf8("输出频率中文说明"));
        const QString path = QDir(m_root.path()).filePath("pdf02.pdf");
        QVERIFY(writeFile(path, fixture.bytes));
        const ManualExtractionResult result = ManualPdfTextExtractor::extractFromFile(path);
        QCOMPARE(modbuslens::ui::manualExtractionStatusToken(result.status), "ok");
        // Exact Unicode assertion: a byte-wise or Latin-1 conversion cannot produce this.
        QCOMPARE(result.pageTexts.at(0), fixture.expectedText);
        QVERIFY(result.pageTexts.at(0).contains(QString::fromUtf8("中文")));
    }

    void pdf03_mixedAsciiAndChinese()
    {
        const PdfFixture fixture = mixedPdf(QStringLiteral("ASCII "), QString::fromUtf8("中文"));
        const QString path = QDir(m_root.path()).filePath("pdf03.pdf");
        QVERIFY(writeFile(path, fixture.bytes));
        const ManualExtractionResult result = ManualPdfTextExtractor::extractFromFile(path);
        QCOMPARE(modbuslens::ui::manualExtractionStatusToken(result.status), "ok");
        QCOMPARE(result.pageTexts.at(0), fixture.expectedText);
    }

    void pdf04_noTextLayerIsExplicitAndNeverOcr()
    {
        const QString path = QDir(m_root.path()).filePath("pdf04.pdf");
        QVERIFY(writeFile(path, noTextPdf()));
        const ManualExtractionResult result = ManualPdfTextExtractor::extractFromFile(path);
        QCOMPARE(modbuslens::ui::manualExtractionStatusToken(result.status),
                 "no_extractable_text");
        QCOMPARE(result.pageCount, 1);
        QCOMPARE(result.totalChars, qint64(0));
        QCOMPARE(result.pageTexts.at(0), QString());
    }

    void pdf05_corruptIsMalformedAndDoesNotCrash()
    {
        QByteArray bytes = asciiPdf(QStringLiteral("truncated")).bytes;
        bytes.truncate(bytes.size() * 55 / 100);
        const QString path = QDir(m_root.path()).filePath("pdf05.pdf");
        QVERIFY(writeFile(path, bytes));
        const ManualExtractionResult result = ManualPdfTextExtractor::extractFromFile(path);
        QCOMPARE(modbuslens::ui::manualExtractionStatusToken(result.status),
                 "malformed_document");
    }

    void pdf06_nonPdfBytesAreRejected()
    {
        const QString path = QDir(m_root.path()).filePath("pdf06.pdf");
        QVERIFY(writeFile(path, QByteArray("this is definitely not a PDF at all").repeated(4)));
        const ManualExtractionResult result = ManualPdfTextExtractor::extractFromFile(path);
        QCOMPARE(modbuslens::ui::manualExtractionStatusToken(result.status),
                 "malformed_document");
    }

    void pdf07_repeatedAndSequentialLifecycle()
    {
        const QString first = QDir(m_root.path()).filePath("pdf07a.pdf");
        const QString second = QDir(m_root.path()).filePath("pdf07b.pdf");
        QVERIFY(writeFile(first, asciiPdf(QStringLiteral("first document")).bytes));
        QVERIFY(writeFile(second, cjkPdf(QString::fromUtf8("第二份文档")).bytes));
        for (int round = 0; round < 3; ++round) {
            const ManualExtractionResult a = ManualPdfTextExtractor::extractFromFile(first);
            const ManualExtractionResult b = ManualPdfTextExtractor::extractFromFile(second);
            QCOMPARE(a.pageTexts.at(0), QStringLiteral("first document"));
            QCOMPARE(b.pageTexts.at(0), QString::fromUtf8("第二份文档"));
        }
    }

    void pdf08_unicodeSourcePath()
    {
        const QString folder = QDir(m_root.path()).filePath(QString::fromUtf8("中文目录"));
        QVERIFY(QDir().mkpath(folder));
        const QString path = QDir(folder).filePath(QString::fromUtf8("验收.pdf"));
        QVERIFY(writeFile(path, asciiPdf(QStringLiteral("unicode path ok")).bytes));
        const ManualExtractionResult result = ManualPdfTextExtractor::extractFromFile(path);
        QCOMPARE(modbuslens::ui::manualExtractionStatusToken(result.status), "ok");
        QCOMPARE(result.pageTexts.at(0), QStringLiteral("unicode path ok"));
    }

    void pdf09_deterministicRepeatedExtraction()
    {
        const PdfFixture fixture = mixedPdf(QStringLiteral("repeat "), QString::fromUtf8("重复"));
        const QString path = QDir(m_root.path()).filePath("pdf09.pdf");
        QVERIFY(writeFile(path, fixture.bytes));
        const QString first = ManualPdfTextExtractor::extractFromFile(path).pageTexts.at(0);
        for (int round = 0; round < 3; ++round) {
            QCOMPARE(ManualPdfTextExtractor::extractFromFile(path).pageTexts.at(0), first);
        }
    }

    void pdf10_sourceLimitIsExplicit()
    {
        // A file larger than the accepted source bound must be refused before loading.
        const QString path = QDir(m_root.path()).filePath("pdf10.pdf");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QByteArray header = "%PDF-1.4\n";
        file.write(header);
        QByteArray filler(1024 * 1024, 'x');
        qint64 remaining = modbuslens::ui::kManualExtractionMaxSourceBytes + 1024;
        while (remaining > 0) {
            file.write(filler);
            remaining -= filler.size();
        }
        file.close();
        const ManualExtractionResult result = ManualPdfTextExtractor::extractFromFile(path);
        QCOMPARE(modbuslens::ui::manualExtractionStatusToken(result.status),
                 "resource_limit_exceeded");
    }

    void pdf11_missingFileIsReported()
    {
        const ManualExtractionResult result = ManualPdfTextExtractor::extractFromFile(
            QDir(m_root.path()).filePath("does-not-exist.pdf"));
        QCOMPARE(modbuslens::ui::manualExtractionStatusToken(result.status),
                 "malformed_document");
    }

    // ================================================================== DOCX ====
    void d01_asciiParagraphs()
    {
        const QString path = QDir(m_root.path()).filePath("d01.docx");
        QVERIFY(writeDocx(path,
                          relationships(officeDocumentRelationship(QStringLiteral(
                              "word/document.xml"))),
                          documentXml(QStringLiteral("<w:p><w:r><w:t>First</w:t></w:r></w:p>"
                                                     "<w:p><w:r><w:t>Second</w:t></w:r></w:p>")),
                          true));
        const ManualExtractionResult result = ManualDocxTextExtractor::extractFromFile(path);
        QCOMPARE(modbuslens::ui::manualExtractionStatusToken(result.status), "ok");
        QCOMPARE(result.mainStoryText, QStringLiteral("First\nSecond\n"));
    }

    void d02_chineseParagraphs()
    {
        const QString path = QDir(m_root.path()).filePath("d02.docx");
        QVERIFY(writeDocx(path,
                          relationships(officeDocumentRelationship(QStringLiteral(
                              "word/document.xml"))),
                          documentXml(QStringLiteral(
                              "<w:p><w:r><w:t>中文段落</w:t></w:r></w:p>")),
                          true));
        const ManualExtractionResult result = ManualDocxTextExtractor::extractFromFile(path);
        QCOMPARE(result.mainStoryText, QString::fromUtf8("中文段落\n"));
    }

    void d03_mixedAsciiAndChinese()
    {
        const QString path = QDir(m_root.path()).filePath("d03.docx");
        QVERIFY(writeDocx(
            path,
            relationships(officeDocumentRelationship(QStringLiteral("word/document.xml"))),
            documentXml(QStringLiteral(
                "<w:p><w:r><w:t>ASCII 与中文 mixed</w:t></w:r></w:p>")),
            true));
        const ManualExtractionResult result = ManualDocxTextExtractor::extractFromFile(path);
        QCOMPARE(result.mainStoryText, QString::fromUtf8("ASCII 与中文 mixed\n"));
    }

    void d04_paragraphSeparatorIsExact()
    {
        const QString path = QDir(m_root.path()).filePath("d04.docx");
        QVERIFY(writeDocx(
            path,
            relationships(officeDocumentRelationship(QStringLiteral("word/document.xml"))),
            documentXml(QStringLiteral("<w:p/><w:p/><w:p/>")), true));
        const ManualExtractionResult result = ManualDocxTextExtractor::extractFromFile(path);
        QCOMPARE(result.mainStoryText, QStringLiteral("\n\n\n"));
    }

    void d05_twoCellTableUsesTab()
    {
        const QString path = QDir(m_root.path()).filePath("d05.docx");
        QVERIFY(writeDocx(
            path,
            relationships(officeDocumentRelationship(QStringLiteral("word/document.xml"))),
            documentXml(QStringLiteral(
                "<w:tbl><w:tr><w:tc><w:p><w:r><w:t>A</w:t></w:r></w:p></w:tc>"
                "<w:tc><w:p><w:r><w:t>B</w:t></w:r></w:p></w:tc></w:tr></w:tbl>")),
            true));
        const ManualExtractionResult result = ManualDocxTextExtractor::extractFromFile(path);
        QCOMPARE(result.mainStoryText, QStringLiteral("A\n\tB\n\t\n"));
    }

    void d06_twoRowTableUsesNewline()
    {
        const QString path = QDir(m_root.path()).filePath("d06.docx");
        QVERIFY(writeDocx(
            path,
            relationships(officeDocumentRelationship(QStringLiteral("word/document.xml"))),
            documentXml(QStringLiteral(
                "<w:tbl>"
                "<w:tr><w:tc><w:p><w:r><w:t>A</w:t></w:r></w:p></w:tc></w:tr>"
                "<w:tr><w:tc><w:p><w:r><w:t>B</w:t></w:r></w:p></w:tc></w:tr>"
                "</w:tbl>")),
            true));
        const ManualExtractionResult result = ManualDocxTextExtractor::extractFromFile(path);
        QCOMPARE(result.mainStoryText, QStringLiteral("A\n\t\nB\n\t\n"));
    }

    void d07_tabElementBecomesTab()
    {
        const QString path = QDir(m_root.path()).filePath("d07.docx");
        QVERIFY(writeDocx(
            path,
            relationships(officeDocumentRelationship(QStringLiteral("word/document.xml"))),
            documentXml(QStringLiteral(
                "<w:p><w:r><w:t>left</w:t><w:tab/><w:t>right</w:t></w:r></w:p>")),
            true));
        const ManualExtractionResult result = ManualDocxTextExtractor::extractFromFile(path);
        QCOMPARE(result.mainStoryText, QStringLiteral("left\tright\n"));
    }

    void d08_breakElementBecomesNewline()
    {
        const QString path = QDir(m_root.path()).filePath("d08.docx");
        QVERIFY(writeDocx(
            path,
            relationships(officeDocumentRelationship(QStringLiteral("word/document.xml"))),
            documentXml(QStringLiteral(
                "<w:p><w:r><w:t>a</w:t><w:br/><w:t>b</w:t></w:r></w:p>")),
            true));
        const ManualExtractionResult result = ManualDocxTextExtractor::extractFromFile(path);
        QCOMPARE(result.mainStoryText, QStringLiteral("a\nb\n"));
    }

    void d09_carriageReturnElementBecomesNewline()
    {
        const QString path = QDir(m_root.path()).filePath("d09.docx");
        QVERIFY(writeDocx(
            path,
            relationships(officeDocumentRelationship(QStringLiteral("word/document.xml"))),
            documentXml(QStringLiteral(
                "<w:p><w:r><w:t>a</w:t><w:cr/><w:t>b</w:t></w:r></w:p>")),
            true));
        const ManualExtractionResult result = ManualDocxTextExtractor::extractFromFile(path);
        QCOMPARE(result.mainStoryText, QStringLiteral("a\nb\n"));
    }

    void d10_adjacentRunsHaveNoSeparator()
    {
        const QString path = QDir(m_root.path()).filePath("d10.docx");
        QVERIFY(writeDocx(
            path,
            relationships(officeDocumentRelationship(QStringLiteral("word/document.xml"))),
            documentXml(QStringLiteral(
                "<w:p><w:r><w:t>one</w:t></w:r><w:r><w:t>two</w:t></w:r></w:p>")),
            true));
        const ManualExtractionResult result = ManualDocxTextExtractor::extractFromFile(path);
        QCOMPARE(result.mainStoryText, QStringLiteral("onetwo\n"));
    }

    void d11_xmlSpaceIsPreserved()
    {
        const QString path = QDir(m_root.path()).filePath("d11.docx");
        QVERIFY(writeDocx(
            path,
            relationships(officeDocumentRelationship(QStringLiteral("word/document.xml"))),
            documentXml(QStringLiteral(
                "<w:p><w:r><w:t xml:space=\"preserve\">  spaced   kept  </w:t></w:r></w:p>")),
            true));
        const ManualExtractionResult result = ManualDocxTextExtractor::extractFromFile(path);
        QCOMPARE(result.mainStoryText, QStringLiteral("  spaced   kept  \n"));
    }

    void d12_relationshipTargetOtherThanWordDocumentXml()
    {
        const QString path = QDir(m_root.path()).filePath("d12.docx");
        QVERIFY(writeDocx(
            path,
            relationships(officeDocumentRelationship(QStringLiteral("custom/mainstory.xml"))),
            documentXml(QStringLiteral("<w:p><w:r><w:t>discovered</w:t></w:r></w:p>")),
            false,
            {{QStringLiteral("custom/mainstory.xml"),
              documentXml(QStringLiteral("<w:p><w:r><w:t>discovered</w:t></w:r></w:p>"))}}));
        const ManualExtractionResult result = ManualDocxTextExtractor::extractFromFile(path);
        QCOMPARE(modbuslens::ui::manualExtractionStatusToken(result.status), "ok");
        QCOMPARE(result.mainStoryText, QStringLiteral("discovered\n"));
    }

    void d13_unicodeFilesystemPath()
    {
        const QString folder = QDir(m_root.path()).filePath(QString::fromUtf8("中文目录"));
        QVERIFY(QDir().mkpath(folder));
        const QString path = QDir(folder).filePath(QString::fromUtf8("说明文档.docx"));
        QVERIFY(writeDocx(
            path,
            relationships(officeDocumentRelationship(QStringLiteral("word/document.xml"))),
            documentXml(QStringLiteral("<w:p><w:r><w:t>unicode docx</w:t></w:r></w:p>")),
            true));
        const ManualExtractionResult result = ManualDocxTextExtractor::extractFromFile(path);
        QCOMPARE(result.mainStoryText, QStringLiteral("unicode docx\n"));
    }

    void d14_corruptZip()
    {
        const QString path = QDir(m_root.path()).filePath("d14.docx");
        QVERIFY(writeFile(path, QByteArray("PK\x03\x04").append(QByteArray(300, 'Z'))));
        const ManualExtractionResult result = ManualDocxTextExtractor::extractFromFile(path);
        QCOMPARE(modbuslens::ui::manualExtractionStatusToken(result.status),
                 "malformed_document");
    }

    void d15_fakeDocxWithoutRels()
    {
        const QString path = QDir(m_root.path()).filePath("d15.docx");
        QVERIFY(writeDocx(path, QByteArray(), QByteArray(), false));
        const ManualExtractionResult result = ManualDocxTextExtractor::extractFromFile(path);
        QCOMPARE(modbuslens::ui::manualExtractionStatusToken(result.status),
                 "malformed_document");
    }

    void d16_missingPackageRelationships()
    {
        const QString path = QDir(m_root.path()).filePath("d16.docx");
        // QByteArray() means "omit the entry entirely": a zero-length _rels/.rels would be a
        // malformed relationship part, not a missing one.
        QVERIFY(writeDocx(path, QByteArray(), documentXml(QStringLiteral("<w:p/>")), true));
        const ManualExtractionResult result = ManualDocxTextExtractor::extractFromFile(path);
        QCOMPARE(modbuslens::ui::manualExtractionStatusToken(result.status),
                 "malformed_document");
        QCOMPARE(result.diagnosticToken, QStringLiteral("package_relationships_missing"));
    }

    void d17_noOfficeDocumentRelationship()
    {
        const QString path = QDir(m_root.path()).filePath("d17.docx");
        QVERIFY(writeDocx(
            path,
            relationships(QStringLiteral(
                "<Relationship Id=\"rId9\" Type=\"http://schemas.openxmlformats.org/package/"
                "2006/relationships/metadata/core-properties\" "
                "Target=\"docProps/core.xml\"/>")),
            documentXml(QStringLiteral("<w:p/>")), true));
        const ManualExtractionResult result = ManualDocxTextExtractor::extractFromFile(path);
        QCOMPARE(result.diagnosticToken,
                 QStringLiteral("office_document_relationship_missing"));
    }

    void d18_relationshipTargetPartMissing()
    {
        const QString path = QDir(m_root.path()).filePath("d18.docx");
        QVERIFY(writeDocx(
            path,
            relationships(officeDocumentRelationship(QStringLiteral("word/document.xml"))),
            documentXml(QStringLiteral("<w:p/>")), false));
        const ManualExtractionResult result = ManualDocxTextExtractor::extractFromFile(path);
        QCOMPARE(result.diagnosticToken, QStringLiteral("main_document_part_missing"));
    }

    void d19_malformedMainXml()
    {
        const QString path = QDir(m_root.path()).filePath("d19.docx");
        QVERIFY(writeDocx(
            path,
            relationships(officeDocumentRelationship(QStringLiteral("word/document.xml"))),
            QByteArray("<?xml version=\"1.0\"?><w:document xmlns:w=\"") + kWordNamespace
                + "\"><w:body><w:p><w:r><w:t>unclosed",
            true));
        const ManualExtractionResult result = ManualDocxTextExtractor::extractFromFile(path);
        QCOMPARE(result.diagnosticToken, QStringLiteral("main_document_xml_malformed"));
    }

    void d20_unsafeRelationshipTargetIsRejected()
    {
        for (const QString &target : {QStringLiteral("../outside.xml"),
                                      QStringLiteral("/absolute.xml"),
                                      QStringLiteral("C:/windows/system32/evil.xml")}) {
            // The fixture name must stay a legal Windows file name: a drive-qualified
            // target contains a colon, which cannot appear in a path component.
            QString fixtureName = QString(target);
            fixtureName.replace(QLatin1Char('/'), QLatin1Char('_'));
            fixtureName.replace(QLatin1Char(':'), QLatin1Char('_'));
            const QString path =
                QDir(m_root.path()).filePath(QStringLiteral("d20_%1.docx").arg(fixtureName));
            QVERIFY(writeDocx(
                path, relationships(officeDocumentRelationship(target)),
                documentXml(QStringLiteral("<w:p><w:r><w:t>x</w:t></w:r></w:p>")), false));
            const ManualExtractionResult result =
                ManualDocxTextExtractor::extractFromFile(path);
            QCOMPARE(result.diagnosticToken, QStringLiteral("unsafe_relationship_target"));
        }
    }

    void d21_headerFooterTextDoesNotLeak()
    {
        const QString path = QDir(m_root.path()).filePath("d21.docx");
        QVERIFY(writeDocx(
            path,
            relationships(officeDocumentRelationship(QStringLiteral("word/document.xml"))),
            documentXml(QStringLiteral("<w:p><w:r><w:t>MAINSTORY</w:t></w:r></w:p>")), true,
            {{QStringLiteral("word/header1.xml"),
              documentXml(QStringLiteral(
                  "<w:p><w:r><w:t>HEADERLEAK</w:t></w:r></w:p>"))},
             {QStringLiteral("word/footer1.xml"),
              documentXml(QStringLiteral(
                  "<w:p><w:r><w:t>FOOTERLEAK</w:t></w:r></w:p>"))}}));
        const ManualExtractionResult result = ManualDocxTextExtractor::extractFromFile(path);
        QCOMPARE(result.mainStoryText, QStringLiteral("MAINSTORY\n"));
        QVERIFY(!result.mainStoryText.contains(QStringLiteral("HEADERLEAK")));
        QVERIFY(!result.mainStoryText.contains(QStringLiteral("FOOTERLEAK")));
    }

    void d22_deterministicRepeatedExtraction()
    {
        const QString path = QDir(m_root.path()).filePath("d22.docx");
        QVERIFY(writeDocx(
            path,
            relationships(officeDocumentRelationship(QStringLiteral("word/document.xml"))),
            documentXml(QStringLiteral(
                "<w:p><w:r><w:t>stable</w:t></w:r></w:p><w:tbl><w:tr><w:tc>"
                "<w:p><w:r><w:t>cell</w:t></w:r></w:p></w:tc></w:tr></w:tbl>")),
            true));
        const QString first = ManualDocxTextExtractor::extractFromFile(path).mainStoryText;
        for (int round = 0; round < 4; ++round) {
            QCOMPARE(ManualDocxTextExtractor::extractFromFile(path).mainStoryText, first);
        }
    }

    void d23_zipEntryCountLimitIsExplicit()
    {
        QList<QPair<QString, QByteArray>> extra;
        for (int index = 0; index < modbuslens::ui::kManualDocxMaxZipEntries + 4; ++index) {
            extra.append({QStringLiteral("word/pad%1.xml").arg(index), QByteArray("<x/>")});
        }
        const QString path = QDir(m_root.path()).filePath("d23.docx");
        QVERIFY(writeDocx(
            path,
            relationships(officeDocumentRelationship(QStringLiteral("word/document.xml"))),
            documentXml(QStringLiteral("<w:p><w:r><w:t>x</w:t></w:r></w:p>")), true, extra));
        const ManualExtractionResult result = ManualDocxTextExtractor::extractFromFile(path);
        QCOMPARE(modbuslens::ui::manualExtractionStatusToken(result.status),
                 "resource_limit_exceeded");
    }

    void d24_emptyStoryIsNoExtractableText()
    {
        const QString path = QDir(m_root.path()).filePath("d24.docx");
        QVERIFY(writeDocx(
            path,
            relationships(officeDocumentRelationship(QStringLiteral("word/document.xml"))),
            documentXml(QString()), true));
        const ManualExtractionResult result = ManualDocxTextExtractor::extractFromFile(path);
        QCOMPARE(modbuslens::ui::manualExtractionStatusToken(result.status),
                 "no_extractable_text");
    }

    void statusTokensAreStable()
    {
        using modbuslens::ui::manualExtractionStatusToken;
        QCOMPARE(QString::fromLatin1(manualExtractionStatusToken(ManualExtractionStatus::Ok)),
                 QStringLiteral("ok"));
        QCOMPARE(QString::fromLatin1(
                     manualExtractionStatusToken(ManualExtractionStatus::NoExtractableText)),
                 QStringLiteral("no_extractable_text"));
        QCOMPARE(QString::fromLatin1(manualExtractionStatusToken(
                     ManualExtractionStatus::MalformedDocument)),
                 QStringLiteral("malformed_document"));
        QCOMPARE(QString::fromLatin1(manualExtractionStatusToken(
                     ManualExtractionStatus::ResourceLimitExceeded)),
                 QStringLiteral("resource_limit_exceeded"));
        QCOMPARE(QString::fromLatin1(manualExtractionStatusToken(
                     ManualExtractionStatus::EncryptedOrPasswordProtected)),
                 QStringLiteral("encrypted_or_password_protected"));
    }

private:
    QTemporaryDir m_root;
};

QTEST_GUILESS_MAIN(ManualExtractionTest)
#include "test_manual_extraction.moc"
