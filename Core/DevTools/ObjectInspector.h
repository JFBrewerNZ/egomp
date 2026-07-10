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

    void Dump(const char* label, const void* object, size_t bytes);
}
