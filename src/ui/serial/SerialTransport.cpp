#include "ui/serial/SerialTransport.h"

// Out-of-line definitions keep the vtable and the moc-emitted meta object in
// exactly one translation unit (the interface is otherwise header-only).
SerialTransport::SerialTransport(QObject* parent)
    : QObject(parent)
{
}

SerialTransport::~SerialTransport() = default;