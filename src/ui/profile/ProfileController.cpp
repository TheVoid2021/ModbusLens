#include "ui/profile/ProfileController.h"

#include "ui/profile/ProfileStore.h"

#include "core/active/ReadRequestParsing.h"
#include "core/active/WriteDraftParsing.h"

#include <QDir>
#include <QFileInfo>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <variant>

namespace modbuslens::ui {

using modbuslens::core::DeviceProfile;

namespace {

QString joinSecondary(const modbuslens::core::DeviceProfile &profile)
{
    QStringList parts;
    for (const std::string *field : {&profile.manufacturer, &profile.model,
                                     &profile.revision}) {
        const QString value = QString::fromStdString(*field);
        if (!value.isEmpty()) {
            parts << value;
        }
    }
    return parts.join(QStringLiteral(" · "));
}

} // namespace

ProfileController::ProfileController(QObject *parent) : QObject(parent)
{
    refreshCatalog();
}

QVariantList ProfileController::profileCatalog() const
{
    return m_catalog;
}

QString ProfileController::catalogIssueText() const
{
    if (m_catalogIssueCount == 0) {
        return QString();
    }
    return QStringLiteral("有 %1 个设备档案无法加载").arg(m_catalogIssueCount);
}

int ProfileController::catalogIssueCount() const
{
    return m_catalogIssueCount;
}

bool ProfileController::hasOpenProfile() const
{
    return m_hasOpenProfile;
}

QString ProfileController::currentProfileId() const
{
    return QString::fromStdString(m_draft.profileId);
}

QString ProfileController::displayName() const
{
    return QString::fromStdString(m_draft.displayName);
}

void ProfileController::setDisplayName(const QString &value)
{
    const std::string converted = value.toStdString();
    if (m_draft.displayName == converted) {
        return;
    }
    m_draft.displayName = converted;
    emitEditorChanged();
}

QString ProfileController::manufacturer() const
{
    return QString::fromStdString(m_draft.manufacturer);
}

void ProfileController::setManufacturer(const QString &value)
{
    const std::string converted = value.toStdString();
    if (m_draft.manufacturer == converted) {
        return;
    }
    m_draft.manufacturer = converted;
    emitEditorChanged();
}

QString ProfileController::model() const
{
    return QString::fromStdString(m_draft.model);
}

void ProfileController::setModel(const QString &value)
{
    const std::string converted = value.toStdString();
    if (m_draft.model == converted) {
        return;
    }
    m_draft.model = converted;
    emitEditorChanged();
}

QString ProfileController::revision() const
{
    return QString::fromStdString(m_draft.revision);
}

void ProfileController::setRevision(const QString &value)
{
    const std::string converted = value.toStdString();
    if (m_draft.revision == converted) {
        return;
    }
    m_draft.revision = converted;
    emitEditorChanged();
}

QString ProfileController::description() const
{
    return QString::fromStdString(m_draft.description);
}

void ProfileController::setDescription(const QString &value)
{
    const std::string converted = value.toStdString();
    if (m_draft.description == converted) {
        return;
    }
    m_draft.description = converted;
    emitEditorChanged();
}

bool ProfileController::dirty() const
{
    return m_hasOpenProfile && m_draft != m_persisted;
}

QString ProfileController::lastActionErrorToken() const
{
    return m_lastActionErrorToken;
}

QString ProfileController::validationText() const
{
    if (!m_hasOpenProfile) {
        return QString();
    }
    const auto validation = modbuslens::core::validateDeviceProfile(m_draft);
    if (validation.code == modbuslens::core::ProfileValidationCode::Ok) {
        return QString();
    }
    if (validation.code
        == modbuslens::core::ProfileValidationCode::DisplayNameMissing) {
        return QStringLiteral("设备名称必填");
    }
    return QStringLiteral("设备档案校验未通过（%1）")
        .arg(QString::fromLatin1(
            modbuslens::core::profileValidationCodeName(validation.code)));
}

QString ProfileController::lastActionError() const
{
    return m_lastActionError;
}

void ProfileController::refreshCatalog()
{
    m_catalog.clear();
    m_catalogIssueCount = 0;
    m_catalogIssueNames.clear();
    const QDir dir(ProfileStore::managedProfilesDirectory());
    // Sorted by filename: the hash-derived names carry no human order, so the
    // catalog ordering below is what decides the presentation.
    const auto files =
        dir.entryInfoList(QStringList{QStringLiteral("profile-*.json")},
                          QDir::Files, QDir::Name);
    struct Row
    {
        QString profileId;
        QString displayName;
        QString secondary;
    };
    QVector<Row> rows;
    for (const QFileInfo &info : files) {
        const auto loaded = ProfileStore::loadFromFile(info.absoluteFilePath());
        if (!loaded.ok()) {
            ++m_catalogIssueCount;
            m_catalogIssueNames << info.fileName();
            continue;
        }
        Row row;
        row.profileId = QString::fromStdString(loaded.profile.profileId);
        row.displayName = QString::fromStdString(loaded.profile.displayName);
        row.secondary = joinSecondary(loaded.profile);
        rows << row;
    }
    // Duplicate displayNames stay distinct identities: profileId is the only
    // authority, and the short form disambiguates the display (T027 §33.4).
    std::sort(rows.begin(), rows.end(),
              [](const Row &a, const Row &b) {
                  if (a.displayName != b.displayName) {
                      return a.displayName < b.displayName;
                  }
                  return a.profileId < b.profileId;
              });
    for (int i = 0; i < rows.size(); ++i) {
        const QString shown = rows[i].displayName;
        const bool duplicate =
            (i > 0 && rows[i - 1].displayName == shown)
            || (i + 1 < rows.size() && rows[i + 1].displayName == shown);
        QVariantMap entry;
        entry.insert(QStringLiteral("profileId"), rows[i].profileId);
        entry.insert(QStringLiteral("displayPrimary"), shown);
        QString secondary = rows[i].secondary;
        if (duplicate || secondary.isEmpty()) {
            const QString shortId = rows[i].profileId.left(8);
            secondary = secondary.isEmpty()
                            ? shortId
                            : QStringLiteral("%1 · %2").arg(secondary, shortId);
        }
        entry.insert(QStringLiteral("displaySecondary"), secondary);
        m_catalog.append(entry);
    }
    emit catalogChanged();
}

void ProfileController::newProfile()
{
    // QUuid is an implementation detail (T027 §24.1): the JSON keeps the
    // logical profileId, and the managed filename derives from it.
    const QString profileId =
        QUuid::createUuid().toString(QUuid::WithoutBraces);
    DeviceProfile profile;
    profile.schemaVersion = 1;
    profile.profileId = profileId.toStdString();
    setDraftFrom(profile);
}

void ProfileController::discardCurrentChanges()
{
    if (!m_hasOpenProfile) {
        return;
    }
    m_draft = m_persisted;
    emitEditorChanged();
}

bool ProfileController::saveCurrent()
{
    m_lastActionError.clear();
    m_lastActionErrorToken.clear();
    if (!m_hasOpenProfile) {
        m_lastActionError = QStringLiteral("没有打开的设备档案");
        emitEditorChanged();
        return false;
    }
    const auto validation = modbuslens::core::validateDeviceProfile(m_draft);
    if (!validation.ok()) {
        // An invalid register entry surfaces here too (a draft that was
        // mutated outside the candidate discipline cannot exist, but the
        // guard stays total): human text + token, and NO partial save.
        m_lastActionErrorToken = QString::fromLatin1(
            modbuslens::core::profileValidationCodeName(validation.code));
        m_lastActionError = validationHumanText(m_draft, validation);
        emitEditorChanged();
        return false;
    }
    const QString profileId = QString::fromStdString(m_draft.profileId);
    const ProfileSaveResult saved = ProfileStore::saveToFile(
        m_draft, ProfileStore::defaultFilePathFor(profileId));
    if (!saved.ok()) {
        m_lastActionError = QStringLiteral("保存失败（%1）")
                                .arg(profileStoreErrorName(saved.error));
        m_lastActionErrorToken = QStringLiteral("save_failed");
        emitEditorChanged();
        return false;
    }
    m_persisted = m_draft;
    m_lastActionError.clear();
    refreshCatalog();
    emitEditorChanged();
    return true;
}

bool ProfileController::openProfile(const QString &profileId)
{
    m_lastActionError.clear();
    m_lastActionErrorToken.clear();
    // openFromDisk only ever applies a fully loaded + validated profile; a
    // refused open leaves the current draft untouched.
    const bool opened = openFromDisk(profileId);
    emitEditorChanged();
    return opened;
}

bool ProfileController::deleteProfile(const QString &profileId)
{
    m_lastActionError.clear();
    m_lastActionErrorToken.clear();
    const QString path = ProfileStore::defaultFilePathFor(profileId);
    QFile file(path);
    if (!file.exists()) {
        m_lastActionError = QStringLiteral("设备档案不存在");
        m_lastActionErrorToken = QStringLiteral("delete_failed");
        emitEditorChanged();
        return false;
    }
    if (!file.remove()) {
        m_lastActionError = QStringLiteral("删除失败（%1）")
                                .arg(file.errorString());
        m_lastActionErrorToken = QStringLiteral("delete_failed");
        emitEditorChanged();
        return false;
    }
    // Deleting the currently open profile clears the editor to
    // No Profile Opened (raw truth / M11 decode are untouched by design).
    if (m_hasOpenProfile
        && m_draft.profileId == profileId.toStdString()) {
        clearEditor();
    }
    refreshCatalog();
    emitEditorChanged();
    return true;
}

void ProfileController::setDraftFrom(const DeviceProfile &profile)
{
    m_persisted = profile;
    m_draft = profile;
    m_hasOpenProfile = true;
    m_lastActionError.clear();
    m_lastActionErrorToken.clear();
    emitEditorChanged();
}

void ProfileController::clearEditor()
{
    m_persisted = DeviceProfile{};
    m_draft = DeviceProfile{};
    m_hasOpenProfile = false;
    m_lastActionError.clear();
    m_lastActionErrorToken.clear();
    emitEditorChanged();
}

void ProfileController::emitEditorChanged()
{
    emit editorChanged();
}

bool ProfileController::openFromDisk(const QString &profileId)
{
    const ProfileLoadResult loaded = ProfileStore::loadFromFile(
        ProfileStore::defaultFilePathFor(profileId));
    if (!loaded.ok()) {
        m_lastActionError = QStringLiteral("打开失败（%1）")
                                .arg(profileStoreErrorName(loaded.error));
        m_lastActionErrorToken.clear();
        return false;
    }
    setDraftFrom(loaded.profile);
    return true;
}

// ---------------------------------------------------------------------------
// Register Map editor (M12-B second slice). Authority discipline identical to
// M10: QML hands over the RAW field texts; the core parsers and the frozen
// profile validation are the ONLY business authorities. Every mutation works
// on a candidate copy of the whole draft — a refused candidate leaves the
// draft byte-identical.
// ---------------------------------------------------------------------------

QVariantList ProfileController::registerMap() const
{
    QVariantList rows;
    if (!m_hasOpenProfile) {
        return rows;
    }
    // Deterministic display order (T027 §17): readFunctionCode, then address.
    // The row carries the DRAFT index, so the editor dialog locates the entry
    // even though the display order differs from the storage order.
    std::vector<int> order;
    order.reserve(m_draft.registers.size());
    for (std::size_t i = 0; i < m_draft.registers.size(); ++i) {
        order.push_back(static_cast<int>(i));
    }
    std::sort(order.begin(), order.end(),
              [this](int a, int b) {
                  const auto &ea = m_draft.registers[static_cast<std::size_t>(a)];
                  const auto &eb = m_draft.registers[static_cast<std::size_t>(b)];
                  if (ea.readFunctionCode != eb.readFunctionCode) {
                      return ea.readFunctionCode < eb.readFunctionCode;
                  }
                  return ea.address < eb.address;
              });
    for (const int index : order) {
        const auto &entry = m_draft.registers[static_cast<std::size_t>(index)];
        QVariantMap row;
        row.insert(QStringLiteral("draftIndex"), index);
        row.insert(QStringLiteral("readFunctionCode"),
                   static_cast<int>(entry.readFunctionCode));
        // Two-uppercase-digit HEX label for the human-readable list (FC03).
        row.insert(QStringLiteral("fcLabel"),
                   QStringLiteral("FC%1")
                       .arg(entry.readFunctionCode, 2, 16, QLatin1Char('0'))
                       .toUpper());
        row.insert(QStringLiteral("address"),
                   static_cast<int>(entry.address));
        row.insert(QStringLiteral("name"),
                   QString::fromStdString(entry.name));
        const int wordCount =
            modbuslens::core::registerDecodeTypeWordCount(entry.dataType);
        row.insert(QStringLiteral("dataType"),
                   QString::fromLatin1(modbuslens::core::profileDataTypeToken(
                       entry.dataType)));
        row.insert(QStringLiteral("span"),
                   wordCount == 1
                       ? QStringLiteral("@%1").arg(entry.address)
                       : QStringLiteral("@%1..%2")
                             .arg(entry.address)
                             .arg(entry.address + wordCount - 1));
        rows.append(row);
    }
    return rows;
}

QVariantMap ProfileController::registerEntryAt(int draftIndex)
{
    QVariantMap out;
    if (!m_hasOpenProfile || draftIndex < 0
        || draftIndex >= static_cast<int>(m_draft.registers.size())) {
        return out;
    }
    const auto &entry = m_draft.registers[static_cast<std::size_t>(draftIndex)];
    out.insert(QStringLiteral("readFunctionCode"),
               static_cast<int>(entry.readFunctionCode));
    out.insert(QStringLiteral("address"), static_cast<int>(entry.address));
    out.insert(QStringLiteral("name"), QString::fromStdString(entry.name));
    out.insert(QStringLiteral("description"),
               QString::fromStdString(entry.description));
    out.insert(QStringLiteral("dataType"),
               static_cast<int>(entry.dataType));
    out.insert(QStringLiteral("registerCount"), entry.registerCount);
    out.insert(QStringLiteral("byteOrder"),
               static_cast<int>(entry.byteOrder));
    out.insert(QStringLiteral("wordOrder"),
               static_cast<int>(entry.wordOrder));
    out.insert(QStringLiteral("scale"), entry.scale);
    out.insert(QStringLiteral("offset"), entry.offset);
    out.insert(QStringLiteral("unit"), QString::fromStdString(entry.unit));
    return out;
}

bool ProfileController::parseEntryFields(const QVariantMap &fields,
                                         modbuslens::core::RegisterEntry &out)
{
    using modbuslens::core::parseReadFunctionCode;
    using modbuslens::core::parseDecimalRegisterValue;
    using modbuslens::core::ValuesParseError;

    const auto rawText = [&fields](const char *key) {
        return fields.value(QLatin1String(key)).toString();
    };

    // ---- readFunctionCode: REQUIRED, no silent default (T027 §32/§35) ----
    const auto fcParsed =
        parseReadFunctionCode(rawText("readFunctionCode").toStdString());
    if (std::holds_alternative<ValuesParseError>(fcParsed)) {
        m_lastActionErrorToken = QStringLiteral("invalid_read_function_code");
        m_lastActionError = QStringLiteral(
            "读取功能码无法解析（须为 01..7F 的 HEX 值，可带 0x 前缀，必填）");
        return false;
    }
    const int functionCode = static_cast<int>(
        std::get<modbuslens::core::ReadFunctionCode>(fcParsed).functionCode);
    if (functionCode < 0x01 || functionCode > 0x7F) {
        m_lastActionErrorToken = QStringLiteral("invalid_read_function_code");
        m_lastActionError = QStringLiteral(
            "读取功能码超出合法域（0x01..0x7F）：0x00 与 0x80..0xFF 是响应"
            "位空间，不能作为读取请求");
        return false;
    }
    out.readFunctionCode = static_cast<std::uint8_t>(functionCode);

    // ---- address: PDU / 0-based decimal input (M10 input convention) ----
    const auto addressParsed =
        parseDecimalRegisterValue(rawText("address").toStdString());
    if (std::holds_alternative<ValuesParseError>(addressParsed)) {
        m_lastActionErrorToken = QStringLiteral("address_out_of_range");
        m_lastActionError = QStringLiteral(
            "寄存器地址无法解析（须为 0..65535 的十进制数值；本应用以 PDU/"
            "0-based 地址为准，不使用 40001 别名）");
        return false;
    }
    out.address = std::get<modbuslens::core::SingleRegisterValue>(addressParsed)
                      .value;

    // ---- name: REQUIRED by the frozen validation; description optional ----
    out.name = rawText("name").toStdString();
    if (out.name.empty()) {
        m_lastActionErrorToken = QStringLiteral("register_name_missing");
        m_lastActionError = QStringLiteral("寄存器条目名称必填");
        return false;
    }
    out.description = rawText("description").toStdString();

    // ---- dataType: the M11 enum index IS the type (no second matrix) ----
    bool dataTypeOk = false;
    const QString dataTypeRaw = rawText("dataType");
    const int dataTypeIndex = dataTypeRaw.isEmpty()
                                  ? static_cast<int>(modbuslens::core::
                                                         RegisterDecodeType::
                                                             UInt16)
                                  : dataTypeRaw.toInt(&dataTypeOk);
    if (!dataTypeOk || dataTypeIndex < 0 || dataTypeIndex > 6) {
        m_lastActionErrorToken = QStringLiteral("unsupported_data_type");
        m_lastActionError = QStringLiteral("数据类型不在 M11 类型矩阵内");
        return false;
    }
    out.dataType =
        static_cast<modbuslens::core::RegisterDecodeType>(dataTypeIndex);
    // registerCount: DERIVED from dataType (Group 1-B) — never a user input;
    // validation still checks it strictly.
    out.registerCount =
        modbuslens::core::registerDecodeTypeWordCount(out.dataType);

    // ---- byteOrder / wordOrder: the M11 enum indices ----
    const int byteOrderIndex = rawText("byteOrder").toInt();
    if (byteOrderIndex
            != static_cast<int>(
                modbuslens::core::RegisterByteOrder::Normal)
        && byteOrderIndex
               != static_cast<int>(
                   modbuslens::core::RegisterByteOrder::ByteSwapped)) {
        m_lastActionErrorToken = QStringLiteral("invalid_byte_order");
        m_lastActionError = QStringLiteral("字节序不在 M11 枚举内");
        return false;
    }
    out.byteOrder =
        static_cast<modbuslens::core::RegisterByteOrder>(byteOrderIndex);
    if (out.registerCount == 2) {
        const int wordOrderIndex = rawText("wordOrder").toInt();
        if (wordOrderIndex
                != static_cast<int>(modbuslens::core::RegisterWordOrder::
                                        HighWordFirst)
            && wordOrderIndex
                   != static_cast<int>(modbuslens::core::RegisterWordOrder::
                                           LowWordFirst)) {
            m_lastActionErrorToken = QStringLiteral("invalid_word_order");
            m_lastActionError = QStringLiteral("字序不在 M11 枚举内");
            return false;
        }
        out.wordOrder =
            static_cast<modbuslens::core::RegisterWordOrder>(wordOrderIndex);
    } else {
        // 1-word type: the wordOrder control is disabled ("不适用"). A -1 (or
        // an absent value) must never pollute the data — keep the M11 default.
        out.wordOrder = modbuslens::core::RegisterWordOrder::HighWordFirst;
    }

    // ---- scale / offset: empty = the frozen defaults; finite required ----
    const QString scaleRaw = rawText("scale").trimmed();
    if (scaleRaw.isEmpty()) {
        out.scale = 1.0; // frozen default (T027 §24.3)
    } else {
        bool scaleOk = false;
        out.scale = scaleRaw.toDouble(&scaleOk);
        if (!scaleOk || !std::isfinite(out.scale)) {
            m_lastActionErrorToken = QStringLiteral("non_finite_scale");
            m_lastActionError = QStringLiteral(
                "缩放系数必须是有效数字（不接受 NaN / 无穷大 / 非数字文本）");
            return false;
        }
    }
    const QString offsetRaw = rawText("offset").trimmed();
    if (offsetRaw.isEmpty()) {
        out.offset = 0.0; // frozen default
    } else {
        bool offsetOk = false;
        out.offset = offsetRaw.toDouble(&offsetOk);
        if (!offsetOk || !std::isfinite(out.offset)) {
            m_lastActionErrorToken = QStringLiteral("non_finite_offset");
            m_lastActionError = QStringLiteral(
                "偏移量必须是有效数字（不接受 NaN / 无穷大 / 非数字文本）");
            return false;
        }
    }
    out.unit = rawText("unit").toStdString();
    return true;
}

QString ProfileController::validationHumanText(
    const modbuslens::core::DeviceProfile &candidate,
    const modbuslens::core::ProfileValidationResult &validation) const
{
    using modbuslens::core::ProfileValidationCode;
    const auto token = QString::fromLatin1(
        modbuslens::core::profileValidationCodeName(validation.code));
    // Duplicate / overlap: name the conflicting entry so the user can find it
    // (the core result only points at the rejected entry).
    if (validation.code == ProfileValidationCode::DuplicateAddress
        || validation.code == ProfileValidationCode::OverlappingSpan) {
        const auto &entry = candidate.registers[static_cast<std::size_t>(
            validation.registerIndex)];
        int conflictIndex = -1;
        for (std::size_t i = 0; i < candidate.registers.size(); ++i) {
            const auto &other = candidate.registers[i];
            const int otherIndex = static_cast<int>(i);
            if (otherIndex == validation.registerIndex
                || other.readFunctionCode != entry.readFunctionCode) {
                continue;
            }
            const int otherLast = other.address + other.registerCount - 1;
            const int entryLast = entry.address + entry.registerCount - 1;
            const bool clashes =
                entry.address <= otherLast && other.address <= entryLast;
            if (clashes) {
                conflictIndex = otherIndex;
                break;
            }
        }
        const QString conflictName =
            conflictIndex >= 0
                ? QString::fromStdString(
                    candidate.registers[static_cast<std::size_t>(conflictIndex)]
                        .name)
                : QStringLiteral("其它条目");
        if (validation.code == ProfileValidationCode::DuplicateAddress) {
            return QStringLiteral("同一功能码 FC%1 空间内地址 %2 已被「%3」"
                                  "占用（重复地址）")
                .arg(entry.readFunctionCode)
                .arg(entry.address)
                .arg(conflictName);
        }
        return QStringLiteral("同一功能码 FC%1 空间内地址范围 %2..%3 与「%4」"
                              "重叠（跨度交叠）")
            .arg(entry.readFunctionCode)
            .arg(entry.address)
            .arg(entry.address + entry.registerCount - 1)
            .arg(conflictName);
    }
    switch (validation.code) {
    case ProfileValidationCode::DisplayNameMissing:
        return QStringLiteral("设备名称必填（先在档案信息中填写，再添加寄存器条目）");
    case ProfileValidationCode::RegisterNameMissing:
        return QStringLiteral("寄存器条目名称必填");
    case ProfileValidationCode::InvalidReadFunctionCode:
        return QStringLiteral("读取功能码超出合法域（0x01..0x7F）");
    case ProfileValidationCode::RegisterCountMismatch:
        return QStringLiteral("寄存器数量与数据类型不一致（应由类型自动决定）");
    case ProfileValidationCode::AddressOutOfRange:
        return QStringLiteral("寄存器地址超出 PDU 范围（0..65535）");
    case ProfileValidationCode::SpanOutOfRange:
        return QStringLiteral("寄存器地址范围超出 PDU 上限（65535）");
    case ProfileValidationCode::NonFiniteScale:
        return QStringLiteral("缩放系数必须是有效数字");
    case ProfileValidationCode::NonFiniteOffset:
        return QStringLiteral("偏移量必须是有效数字");
    default:
        break;
    }
    return QStringLiteral("设备档案校验未通过（%1）").arg(token);
}

bool ProfileController::addRegisterEntry(const QVariantMap &fields)
{
    m_lastActionError.clear();
    m_lastActionErrorToken.clear();
    if (!m_hasOpenProfile) {
        m_lastActionError = QStringLiteral("没有打开的设备档案");
        emitEditorChanged();
        return false;
    }
    modbuslens::core::RegisterEntry entry;
    if (!parseEntryFields(fields, entry)) {
        emitEditorChanged();
        return false;
    }
    auto candidate = m_draft;
    candidate.registers.push_back(entry);
    const auto validation = modbuslens::core::validateDeviceProfile(candidate);
    if (!validation.ok()) {
        m_lastActionErrorToken = QString::fromLatin1(
            modbuslens::core::profileValidationCodeName(validation.code));
        m_lastActionError = validationHumanText(candidate, validation);
        emitEditorChanged();
        return false;
    }
    m_draft = candidate;
    emitEditorChanged();
    return true;
}

bool ProfileController::editRegisterEntry(int draftIndex,
                                          const QVariantMap &fields)
{
    m_lastActionError.clear();
    m_lastActionErrorToken.clear();
    if (!m_hasOpenProfile || draftIndex < 0
        || draftIndex >= static_cast<int>(m_draft.registers.size())) {
        m_lastActionError = QStringLiteral("寄存器条目不存在或已被删除");
        m_lastActionErrorToken = QStringLiteral("register_entry_missing");
        emitEditorChanged();
        return false;
    }
    modbuslens::core::RegisterEntry entry;
    if (!parseEntryFields(fields, entry)) {
        emitEditorChanged();
        return false;
    }
    auto candidate = m_draft;
    candidate.registers[static_cast<std::size_t>(draftIndex)] = entry;
    const auto validation = modbuslens::core::validateDeviceProfile(candidate);
    if (!validation.ok()) {
        m_lastActionErrorToken = QString::fromLatin1(
            modbuslens::core::profileValidationCodeName(validation.code));
        m_lastActionError = validationHumanText(candidate, validation);
        emitEditorChanged();
        return false;
    }
    m_draft = candidate;
    emitEditorChanged();
    return true;
}

bool ProfileController::removeRegisterEntry(int draftIndex)
{
    m_lastActionError.clear();
    m_lastActionErrorToken.clear();
    if (!m_hasOpenProfile || draftIndex < 0
        || draftIndex >= static_cast<int>(m_draft.registers.size())) {
        m_lastActionError = QStringLiteral("寄存器条目不存在或已被删除");
        m_lastActionErrorToken = QStringLiteral("register_entry_missing");
        emitEditorChanged();
        return false;
    }
    // Draft-only mutation (T027 §16): the managed JSON changes only through
    // Save; Discard restores the persisted map byte-identically.
    m_draft.registers.erase(m_draft.registers.begin()
                            + static_cast<std::ptrdiff_t>(draftIndex));
    emitEditorChanged();
    return true;
}

bool ProfileController::applyCandidateField(const QString &fieldToken,
                                            const QString &value)
{
    // M12-C C3 (T027 §81): the controlled Candidate-value write. Same staged
    // discipline as the register editor — whole-draft copy, one supported
    // field, FULL profile validation, commit-once — and never a Save.
    m_lastActionError.clear();
    m_lastActionErrorToken.clear();
    if (!m_hasOpenProfile) {
        m_lastActionError = QStringLiteral("没有打开的设备档案");
        emitEditorChanged();
        return false;
    }
    // v1 whitelist (C3-H10): the C2 first slice can only produce a
    // Manufacturer candidate. Everything else is an explicit refusal — never
    // a silent generalization over the six enum members.
    if (fieldToken != QStringLiteral("manufacturer")) {
        m_lastActionErrorToken = QStringLiteral("unsupported_candidate_field");
        m_lastActionError = QStringLiteral("暂不支持将该候选字段写入设备档案");
        emitEditorChanged();
        return false;
    }
    auto candidate = m_draft;
    candidate.manufacturer = value.toStdString();
    const auto validation = modbuslens::core::validateDeviceProfile(candidate);
    if (!validation.ok()) {
        m_lastActionErrorToken = QString::fromLatin1(
            modbuslens::core::profileValidationCodeName(validation.code));
        m_lastActionError = validationHumanText(candidate, validation);
        emitEditorChanged();
        return false;
    }
    m_draft = candidate;
    emitEditorChanged();
    return true;
}

void ProfileController::clearActionError()
{
    if (m_lastActionError.isEmpty() && m_lastActionErrorToken.isEmpty()) {
        return;
    }
    m_lastActionError.clear();
    m_lastActionErrorToken.clear();
    emitEditorChanged();
}

} // namespace modbuslens::ui
