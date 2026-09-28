#include <QtTest>

#include <QDir>
#include <QFile>
#include <QMetaMethod>
#include <QMetaObject>
#include <QTemporaryDir>
#include <QVariantMap>

#include "core/manual/ManualDocument.h"
#include "core/profile/DeviceProfile.h"
#include "ui/manual/ManualImportController.h"
#include "ui/manual/ManualStore.h"
#include "ui/profile/ActiveProfileController.h"
#include "ui/profile/ProfileController.h"
#include "ui/profile/ProfileStore.h"

using modbuslens::core::DeviceProfile;
using modbuslens::core::ManualDocumentType;
using modbuslens::core::ManualImportError;
using modbuslens::core::manualImportErrorToken;
using modbuslens::ui::ActiveProfileController;
using modbuslens::ui::ManualImportController;
using modbuslens::ui::ManualStore;
using modbuslens::ui::ProfileController;
using modbuslens::ui::ProfileStore;

namespace {

// Every test runs against an ISOLATED temporary managed root: the production
// user-data directory is never touched and no absolute user path exists in
// this file.
class ManagedRootScope
{
public:
    explicit ManagedRootScope()
    {
        root_ = new QTemporaryDir();
        ManualStore::setManagedRootOverride(root_->path());
        ProfileStore::setManagedRootOverride(root_->path());
    }
    ~ManagedRootScope()
    {
        ManualStore::setManagedRootOverride(QString());
        ProfileStore::setManagedRootOverride(QString());
        delete root_;
    }
    [[nodiscard]] QString path() const { return root_->path(); }
    [[nodiscard]] bool valid() const { return root_->isValid(); }

private:
    QTemporaryDir *root_{nullptr};
};

[[nodiscard]] QString writeSource(const QString &dir, const QString &name,
                                  const QByteArray &bytes)
{
    const QString path = QDir(dir).filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(bytes);
    file.close();
    return path;
}

[[nodiscard]] int fileCount(const QString &dir)
{
    return QDir(dir).entryList(QDir::Files).size();
}

} // namespace

class ManualImportTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(QByteArray("\xE4\xB8\xAD\xE6\x96\x87").size() == 6);
    }

    // ---- decoding ----
    void utf8TxtSucceeds()
    {
        ManagedRootScope root;
        QVERIFY(root.valid());
        const QString path = writeSource(root.path(), "plain.txt",
                                         QByteArray("Frequency register 1000\n"));
        QVERIFY(!path.isEmpty());
        const auto result = ManualStore::importSourceFile(path, "plain.txt");
        QVERIFY2(result.ok(), qPrintable(QString::fromStdString(
                                  std::string(manualImportErrorToken(
                                      result.error)))));
        QVERIFY(result.document.documentType == ManualDocumentType::Txt);
        bool ok = false;
        const QString text =
            ManualStore::loadText(QString::fromStdString(
                                      result.document.contentHash), &ok);
        QVERIFY(ok);
        QCOMPARE(text, QStringLiteral("Frequency register 1000\n"));
    }

    void utf8BomIsStripped()
    {
        ManagedRootScope root;
        QVERIFY(root.valid());
        QByteArray bytes;
        bytes.append("\xEF\xBB\xBF", 3);
        bytes.append("BOM stripped\n");
        const QString path =
            writeSource(root.path(), "bom.txt", bytes);
        QVERIFY(!path.isEmpty());
        const auto result = ManualStore::importSourceFile(path, "bom.txt");
        QVERIFY(result.ok());
        bool ok = false;
        const QString text =
            ManualStore::loadText(QString::fromStdString(
                                      result.document.contentHash), &ok);
        QVERIFY(ok);
        QVERIFY2(!text.contains(QChar(0xFEFF)), "BOM leaked into the text");
        QCOMPARE(text, QStringLiteral("BOM stripped\n"));
    }

    void markdownSucceeds()
    {
        ManagedRootScope root;
        QVERIFY(root.valid());
        const QString path = writeSource(root.path(), "manual.md",
                                         QByteArray("# Heading\n\nBody text\n"));
        QVERIFY(!path.isEmpty());
        const auto result = ManualStore::importSourceFile(path, "manual.md");
        QVERIFY(result.ok());
        QVERIFY(result.document.documentType == ManualDocumentType::Markdown);
    }

    void chineseAndEmojiAreNotBinary()
    {
        ManagedRootScope root;
        QVERIFY(root.valid());
        const QByteArray bytes =
            "\xE8\xBE\x93\xE5\x87\xBA\xE9\xA2\x91\xE7\x8E\x87 "
            "\xF0\x9F\x94\xA7 46.6 Hz\n";
        const QString path = writeSource(root.path(), "cn.txt", bytes);
        QVERIFY(!path.isEmpty());
        const auto result = ManualStore::importSourceFile(path, "cn.txt");
        QVERIFY2(result.ok(), qPrintable(QString::fromStdString(
                                  std::string(manualImportErrorToken(
                                      result.error)))));
        bool ok = false;
        const QString text =
            ManualStore::loadText(QString::fromStdString(
                                      result.document.contentHash), &ok);
        QVERIFY(ok);
        QVERIFY(text.contains(QString::fromUtf8("\xE8\xBE\x93\xE5\x87\xBA"
                                                "\xE9\xA2\x91\xE7\x8E\x87")));
        QVERIFY(text.contains(QStringLiteral("46.6 Hz")));
    }

    void invalidUtf8IsRejected()
    {
        ManagedRootScope root;
        QVERIFY(root.valid());
        const QByteArray bytes =
            QByteArray("valid ") + QByteArray("\xFF\xFE\x00\x80", 4);
        const QString path = writeSource(root.path(), "bad.txt", bytes);
        QVERIFY(!path.isEmpty());
        ManualImportController controller;
        const auto before = ManualStore::loadAll().size();
        const bool imported =
            controller.importManualFile(QUrl::fromLocalFile(path));
        QVERIFY(!imported);
        QCOMPARE(controller.lastErrorToken(),
                 QString::fromStdString(
                     std::string(
                         manualImportErrorToken(
                             ManualImportError::BinaryContent))));
        QCOMPARE(static_cast<int>(ManualStore::loadAll().size()),
                 static_cast<int>(before));
    }

    void invalidUtf8WithoutNulIsRejected()
    {
        ManagedRootScope root;
        QVERIFY(root.valid());
        // A lone continuation byte: invalid UTF-8, no NUL byte anywhere.
        const QByteArray bytes = QByteArray("abc") + QByteArray("\x80", 1)
                                 + QByteArray("def");
        const QString path = writeSource(root.path(), "bad2.txt", bytes);
        QVERIFY(!path.isEmpty());
        const auto result = ManualStore::importSourceFile(path, "bad2.txt");
        QVERIFY(!result.ok());
        QCOMPARE(QString::fromStdString(
                     std::string(manualImportErrorToken(result.error))),
                 QString::fromStdString(
                     std::string(manualImportErrorToken(
                         ManualImportError::InvalidEncoding))));
    }

    void utf16BomIsUnsupportedNotBinary()
    {
        ManagedRootScope root;
        QVERIFY(root.valid());
        QByteArray bytes;
        bytes.append("\xFF\xFE", 2);
        bytes.append("A\x00B\x00", 4);
        const QString path = writeSource(root.path(), "utf16.txt", bytes);
        QVERIFY(!path.isEmpty());
        const auto result = ManualStore::importSourceFile(path, "utf16.txt");
        QVERIFY(!result.ok());
        QCOMPARE(QString::fromStdString(
                     std::string(manualImportErrorToken(result.error))),
                 QString::fromStdString(
                     std::string(manualImportErrorToken(
                         ManualImportError::UnsupportedEncoding))));
    }

    void binaryWithNulIsRejected()
    {
        ManagedRootScope root;
        QVERIFY(root.valid());
        QByteArray bytes("PK\x03\x04", 4);
        bytes.append('\0');
        bytes.append("rest");
        const QString path = writeSource(root.path(), "blob.txt", bytes);
        QVERIFY(!path.isEmpty());
        const auto result = ManualStore::importSourceFile(path, "blob.txt");
        QVERIFY(!result.ok());
        QCOMPARE(QString::fromStdString(
                     std::string(manualImportErrorToken(result.error))),
                 QString::fromStdString(
                     std::string(manualImportErrorToken(
                         ManualImportError::BinaryContent))));
    }

    void unsupportedExtensionIsRejected()
    {
        ManagedRootScope root;
        QVERIFY(root.valid());
        // M12-C C1b second slice (T027 §60.1): .pdf / .docx are now SUPPORTED
        // routes, so the unsupported-extension case must use an extension that
        // is still outside the frozen matrix. The rejection is extension-level
        // and happens before any content is read.
        const QString path =
            writeSource(root.path(), "manual.exe", QByteArray("MZ binary"));
        QVERIFY(!path.isEmpty());
        const auto result = ManualStore::importSourceFile(path, "manual.exe");
        QVERIFY(!result.ok());
        QCOMPARE(QString::fromStdString(
                     std::string(manualImportErrorToken(result.error))),
                 QString::fromStdString(
                     std::string(manualImportErrorToken(
                         ManualImportError::UnsupportedType))));
    }

    void tooLargeIsRejectedWithoutTruncation()
    {
        ManagedRootScope root;
        QVERIFY(root.valid());
        const QByteArray bytes(static_cast<int>(
                                   modbuslens::core::kManualMaxSourceBytes) + 1,
                               'a');
        const QString path = writeSource(root.path(), "big.txt", bytes);
        QVERIFY(!path.isEmpty());
        const auto result = ManualStore::importSourceFile(path, "big.txt");
        QVERIFY(!result.ok());
        QCOMPARE(QString::fromStdString(
                     std::string(manualImportErrorToken(result.error))),
                 QString::fromStdString(
                     std::string(manualImportErrorToken(
                         ManualImportError::TooLarge))));
        QCOMPARE(fileCount(ManualStore::documentsDirectory()), 0);
    }

    // ---- content identity / cache ----
    void contentHashIsDeterministic()
    {
        ManagedRootScope root;
        QVERIFY(root.valid());
        const QString a = writeSource(root.path(), "a.txt",
                                      QByteArray("same bytes\n"));
        const QString b = writeSource(root.path(), "b.txt",
                                      QByteArray("same bytes\n"));
        QVERIFY(!a.isEmpty() && !b.isEmpty());
        const auto ra = ManualStore::importSourceFile(a, "a.txt");
        const auto rb = ManualStore::importSourceFile(b, "b.txt");
        QVERIFY(ra.ok() && rb.ok());
        QCOMPARE(QString::fromStdString(ra.document.contentHash),
                 QString::fromStdString(rb.document.contentHash));
        // Same content => one cache entry, but TWO distinct documents: C1a
        // never silently merges two Human imports.
        QVERIFY(ra.document.documentId != rb.document.documentId);
        QCOMPARE(fileCount(ManualStore::textDirectory()), 1);
        QCOMPARE(fileCount(ManualStore::sourceDirectory()), 1);
    }

    void managedCopyAndCacheAreCreated()
    {
        ManagedRootScope root;
        QVERIFY(root.valid());
        const QString path = writeSource(root.path(), "copy.txt",
                                         QByteArray("payload\n"));
        QVERIFY(!path.isEmpty());
        const auto result = ManualStore::importSourceFile(path, "copy.txt");
        QVERIFY(result.ok());
        const QString hash = QString::fromStdString(result.document.contentHash);
        QVERIFY(QFileInfo::exists(QDir(ManualStore::sourceDirectory())
                                      .filePath(hash + ".bin")));
        QVERIFY(QFileInfo::exists(QDir(ManualStore::textDirectory())
                                      .filePath(hash + ".txt")));
    }

    void metadataPersistsAndReloadDiscoversTheDocument()
    {
        ManagedRootScope root;
        QVERIFY(root.valid());
        const QString path = writeSource(root.path(), "reload.txt",
                                         QByteArray("persisted\n"));
        QVERIFY(!path.isEmpty());
        const auto result = ManualStore::importSourceFile(path, "reload.txt");
        QVERIFY(result.ok());
        const auto reloaded = ManualStore::loadAll();
        QCOMPARE(static_cast<int>(reloaded.size()), 1);
        QCOMPARE(QString::fromStdString(reloaded.at(0).documentId),
                 QString::fromStdString(result.document.documentId));
        QCOMPARE(QString::fromStdString(reloaded.at(0).contentHash),
                 QString::fromStdString(result.document.contentHash));
    }

    void originalSourceDeletionDoesNotBreakTheManagedDocument()
    {
        ManagedRootScope root;
        QVERIFY(root.valid());
        const QString path = writeSource(root.path(), "movable.txt",
                                         QByteArray("still here\n"));
        QVERIFY(!path.isEmpty());
        const auto result = ManualStore::importSourceFile(path, "movable.txt");
        QVERIFY(result.ok());
        QVERIFY(QFile::remove(path));
        QVERIFY(!QFileInfo::exists(path));
        const auto reloaded = ManualStore::loadAll();
        QCOMPARE(static_cast<int>(reloaded.size()), 1);
        QVERIFY(ManualStore::hasManagedArtifacts(reloaded.at(0)));
        bool ok = false;
        const QString text =
            ManualStore::loadText(QString::fromStdString(
                                      reloaded.at(0).contentHash), &ok);
        QVERIFY(ok);
        QCOMPARE(text, QStringLiteral("still here\n"));
    }

    // ---- import transaction safety ----
    void failedImportLeavesNoPartialDocumentOrCache()
    {
        ManagedRootScope root;
        QVERIFY(root.valid());
        const QString good = writeSource(root.path(), "good.txt",
                                         QByteArray("good\n"));
        QVERIFY(!good.isEmpty());
        QVERIFY(ManualStore::importSourceFile(good, "good.txt").ok());
        const int docsBefore = fileCount(ManualStore::documentsDirectory());
        const int cacheBefore = fileCount(ManualStore::textDirectory());

        const QString bad = writeSource(root.path(), "bad.pdf",
                                        QByteArray("%PDF-1.7\n"));
        QVERIFY(!bad.isEmpty());
        ManualImportController controller;
        QVERIFY(!controller.importManualFile(QUrl::fromLocalFile(bad)));
        QCOMPARE(fileCount(ManualStore::documentsDirectory()), docsBefore);
        QCOMPARE(fileCount(ManualStore::textDirectory()), cacheBefore);
        QCOMPARE(static_cast<int>(ManualStore::loadAll().size()), docsBefore);
    }

    // ---- Markdown safety ----
    void markdownImportIsPlainTextAndTriggersNoSideEffects()
    {
        ManagedRootScope root;
        QVERIFY(root.valid());
        const QByteArray bytes =
            "# Heading\n\n<script>alert(1)</script>\n\n"
            "![x](https://example.invalid/x.png)\n\n"
            "[link](https://example.invalid/page)\n";
        const QString path = writeSource(root.path(), "unsafe.md", bytes);
        QVERIFY(!path.isEmpty());
        ManualImportController controller;
        QVERIFY(controller.importManualFile(QUrl::fromLocalFile(path)));
        const QString preview = controller.previewText();
        QVERIFY(preview.contains(QStringLiteral("<script>alert(1)</script>")));
        QVERIFY(preview.contains(
            QStringLiteral("![x](https://example.invalid/x.png)")));
        QVERIFY(preview.contains(
            QStringLiteral("[link](https://example.invalid/page)")));
        // Imported verbatim as source text: nothing was executed or fetched.
        QCOMPARE(preview, QString::fromUtf8(bytes));
    }

    // ---- evidence foundation ----
    void evidenceReferenceAttachesToTheManagedSource()
    {
        ManagedRootScope root;
        QVERIFY(root.valid());
        const QByteArray bytes = "Frequency register 1000 maps to 46.6 Hz\n";
        const QString path = writeSource(root.path(), "evidence.txt", bytes);
        QVERIFY(!path.isEmpty());
        ManualImportController controller;
        QVERIFY(controller.importManualFile(QUrl::fromLocalFile(path)));
        const QVariantMap reference = controller.evidenceReferenceAt(0, 10);
        QCOMPARE(reference.value("documentId").toString(),
                 controller.selectedDocument().value("documentId").toString());
        QCOMPARE(reference.value("contentHash").toString(),
                 controller.selectedDocument().value("contentHash").toString());
        // A page number is NEVER fabricated for TXT / Markdown.
        QCOMPARE(reference.value("pageNumber").toInt(), -1);
        QCOMPARE(reference.value("textStart").toInt(), 0);
        QCOMPARE(reference.value("textEnd").toInt(), 10);
        QCOMPARE(reference.value("excerpt").toString(),
                 QStringLiteral("Frequency "));
    }

    // ---- Profile / Active Profile isolation ----
    void profileDraftAndDirtyStateAreUntouched()
    {
        ManagedRootScope root;
        QVERIFY(root.valid());
        ProfileController profiles;
        profiles.newProfile();
        profiles.setDisplayName(QStringLiteral("Draft Profile"));
        QVERIFY(profiles.hasOpenProfile());
        const QString displayBefore = profiles.displayName();
        const bool dirtyBefore = profiles.dirty();
        const int catalogBefore = profiles.profileCatalog().size();

        const QString path = writeSource(root.path(), "iso.txt",
                                         QByteArray("isolation\n"));
        QVERIFY(!path.isEmpty());
        ManualImportController controller;
        QVERIFY(controller.importManualFile(QUrl::fromLocalFile(path)));

        QCOMPARE(profiles.displayName(), displayBefore);
        QCOMPARE(profiles.dirty(), dirtyBefore);
        QCOMPARE(profiles.profileCatalog().size(), catalogBefore);
        QVERIFY(profiles.hasOpenProfile());

        // The import must not switch or infer an Active Profile either.
        ActiveProfileController active;
        active.setProfileController(&profiles);
        const bool activeBefore = active.hasActiveProfile();
        QVERIFY(!activeBefore);
        const QString path2 = writeSource(root.path(), "iso2.txt",
                                          QByteArray("isolation 2\n"));
        QVERIFY(!path2.isEmpty());
        QVERIFY(controller.importManualFile(QUrl::fromLocalFile(path2)));
        QCOMPARE(active.hasActiveProfile(), activeBefore);
        QCOMPARE(active.activeProfileId(), QString());
    }

    void persistedProfileJsonIsUntouched()
    {
        ManagedRootScope root;
        QVERIFY(root.valid());
        DeviceProfile profile;
        profile.schemaVersion = 1;
        profile.profileId = "profile-iso";
        profile.displayName = "Saved Profile";
        const QString profilePath =
            ProfileStore::defaultFilePathFor(QStringLiteral("profile-iso"));
        QVERIFY(ProfileStore::saveToFile(profile, profilePath).ok());
        QFile before(profilePath);
        QVERIFY(before.open(QIODevice::ReadOnly));
        const QByteArray beforeBytes = before.readAll();
        before.close();

        const QString path = writeSource(root.path(), "iso3.txt",
                                         QByteArray("isolation 3\n"));
        QVERIFY(!path.isEmpty());
        ManualImportController controller;
        QVERIFY(controller.importManualFile(QUrl::fromLocalFile(path)));

        QFile after(profilePath);
        QVERIFY(after.open(QIODevice::ReadOnly));
        const QByteArray afterBytes = after.readAll();
        after.close();
        QCOMPARE(afterBytes, beforeBytes);
    }

    // ---- no AI / network / credential surface ----
    void controllerExposesNoAiNetworkCredentialSurface()
    {
        const QMetaObject *meta = &ManualImportController::staticMetaObject;
        QStringList names;
        for (int i = 0; i < meta->methodCount(); ++i) {
            names << QString::fromLatin1(meta->method(i).methodSignature());
        }
        const QStringList forbidden = {
            QStringLiteral("ai"),          QStringLiteral("candidate"),
            QStringLiteral("network"),     QStringLiteral("credential"),
            QStringLiteral("apikey"),      QStringLiteral("provider"),
            QStringLiteral("question"),    QStringLiteral("chat"),
            QStringLiteral("upload"),      QStringLiteral("accept"),
            QStringLiteral("reject"),      QStringLiteral("cloud"),
        };
        for (const QString &line : names) {
            const QString lower = line.toLower();
            for (const QString &token : forbidden) {
                QVERIFY2(!lower.contains(token),
                         qPrintable(QStringLiteral("C1a controller exposes a "
                                                   "forbidden surface: %1")
                                        .arg(line)));
            }
        }
    }
};

QTEST_GUILESS_MAIN(ManualImportTest)
#include "test_manual_import.moc"
