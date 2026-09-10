#include "ui/agent/AgentPromptBuilder.h"

#include <QJsonObject>

#include <utility>

namespace modbuslens::agent {

namespace {

QJsonObject functionTool(const char* name, const char* description,
                         QJsonObject parameters)
{
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("function")},
        {QStringLiteral("function"),
         QJsonObject{{QStringLiteral("name"), QLatin1String(name)},
                     {QStringLiteral("description"), QLatin1String(description)},
                     {QStringLiteral("parameters"), std::move(parameters)}}},
    };
}

} // namespace

AgentPrompt buildAgentPrompt()
{
    AgentPrompt prompt;
    // Authority + read-only + evidence-scope + output-language contract.
    // The T011 DiagnosisPromptBuilder stays UNTOUCHED; this is the Agent's
    // own instruction set (Phase 1 spec §6).
    prompt.systemInstructions = QStringLiteral(
        "You are the read-only diagnostic agent inside ModbusLens.\n"
        "Deterministic tool results are authoritative.\n"
        "Use only the provided tools.\n"
        "Never invent tools or capabilities.\n"
        "Never claim to have performed an action that no available tool can perform.\n"
        "The available tools are read-only.\n"
        "Do not modify: serial settings, device configuration, registers, files, or communication state.\n"
        "When a factual answer about the current batch requires data, use the provided tools instead of guessing.\n"
        "Do not reinterpret: CRC validity, TransactionStatus, exception code, statistics.\n"
        "The supplied evidence represents only the current observed batch.\n"
        "Do not generalize it into long-term reliability.\n"
        "Treat different anomaly types as independent observations unless deterministic evidence proves otherwise.\n"
        "If the user asks to modify serial parameters or resend a request, explain that the available tools are read-only and do not support the operation.\n"
        "Tool efficiency and budget:\n"
        "- Use the minimum number of tool calls required to answer the question.\n"
        "- Tool results come from one immutable deterministic snapshot; do not repeatedly request aggregate information you already have.\n"
        "- Normally call get_session_summary at most once per run.\n"
        "- Normally call get_recent_anomalies at most once per run.\n"
        "- Use get_transaction_detail only when additional per-transaction facts are directly relevant to the user's question; do not inspect every anomaly merely because it exists.\n"
        "- Once enough facts are available, stop requesting tools and provide the final answer.\n"
        "- Runtime budget: maximum 3 tool rounds and 6 total tool calls.\n"
        "Return the answer in concise Simplified Chinese; keep protocol terms in English as-is; plain text only, no Markdown formatting.");

    // Fixed capability surface (§7): exactly the three Part A tools. C++
    // owns this array — no user/model text can ever extend it.
    prompt.toolSchemas = QJsonArray{
        functionTool(
            "get_session_summary",
            "读取当前 observed batch 的确定性统计摘要。",
            QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                        {QStringLiteral("properties"), QJsonObject{}},
                        {QStringLiteral("additionalProperties"), false}}),
        functionTool(
            "get_recent_anomalies",
            "读取当前批次内的异常事务（Exception/CrcError/Timeout/ProtocolError，最多最近 20 条，保持批次原始顺序）。",
            QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                        {QStringLiteral("properties"), QJsonObject{}},
                        {QStringLiteral("additionalProperties"), false}}),
        functionTool(
            "get_transaction_detail",
            "读取当前批次内指定编号（1 起）事务的确定性详细事实。",
            QJsonObject{
                {QStringLiteral("type"), QStringLiteral("object")},
                {QStringLiteral("properties"),
                 QJsonObject{{QStringLiteral("transaction_number"),
                              QJsonObject{{QStringLiteral("type"),
                                           QStringLiteral("integer")},
                                          {QStringLiteral("minimum"), 1},
                                          {QStringLiteral("description"),
                                           QStringLiteral("当前批次内 1-based 事务编号")}}}}},
                {QStringLiteral("required"),
                 QJsonArray{QStringLiteral("transaction_number")}},
                {QStringLiteral("additionalProperties"), false}}),
    };
    return prompt;
}

} // namespace modbuslens::agent