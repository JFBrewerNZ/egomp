#pragma once

#include <cstddef>

// Runtime structure discovery for reverse engineering. Fable.exe ships with
// full MSVC RTTI, so any polymorphic game object can be identified by class
// name at runtime: vptr -> CompleteObjectLocator -> TypeDescriptor -> name.
// Dump() walks an object's memory and reports every member that points at an
// RTTI-identifiable object (directly, or one indirection deep for arrays and
// lists of component pointers), giving member offsets without a disassembler.
// Output goes to the console and is appended to EgoMP-inspect.log next to
// the DLL.
namespace ObjectInspector
{
    // Mangled class name (".?AVCTCInventoryClothing@@") from a polymorphic
    // object's RTTI, or nullptr if the pointer doesn't lead to a valid
    // vtable/RTTI chain inside Fable.exe.
    const char* GetRttiName(const void* object);

    // VirtualQuery-based readability check, exported for other probes.
    bool IsReadableMemory(const void* p, size_t bytes);

    void Dump(const char* label, const void* object, size_t bytes);

    // Every dword of a buffer: index, hex value, RTTI class if it points at
    // an object. For understanding list/pair layouts (e.g. the creature's
    // thing-component list).
    void DumpRaw(const char* label, const void* buffer, size_t dwords);

    // Scans a buffer of dwords and runs Dump() on each pointed-at object
    // whose RTTI class name contains any of the given substrings.
    void DumpMatchingObjects(const char* label, const void* buffer, size_t dwords,
        const char* const* substrings, size_t substringCount, size_t objectBytes);
}
