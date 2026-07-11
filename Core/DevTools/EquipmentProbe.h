#pragma once

class CThingPlayerCreature;

// Reverse-engineering probes for equipment sync (see Tools/RE-NOTES.md).
// Layout knowledge (TC list at CThing+0x44, worn-armour array at
// CTCInventoryClothing+0x14C, weapon slots at CTCInventoryWeapons+0x140/
// +0x150) was recovered with ObjectInspector dumps and serializer
// disassembly on 2026-07-10.
namespace EquipmentProbe
{
    // NUMPAD8: log the def names of everything the creature wears/carries.
    // Read-only; this is the future send-side of equipment sync.
    void DumpEquipment(CThingPlayerCreature* creature);

    // NUMPAD9: call the next unidentified CTCInventoryClothing vfunc
    // candidate with the first worn piece as argument, SEH-guarded, and log
    // what happened. Iterates one candidate per call; watch the hero for
    // visible changes to identify equip/unequip functions.
    void ProbeNextCandidate(CThingPlayerCreature* creature);

    // NUMPAD8: one factory-mode hypothesis per press — create a broadsword
    // with the next candidate mode, assign it to the carried-melee holder
    // (+0x134) and RegenerateCarriedWeapons; restores the original weapon
    // on fault. Success = the broadsword appears on the hero's back. This
    // is the puppet weapon-visual apply path.
    void WeaponEquipProbe(CThingPlayerCreature* creature);
}
