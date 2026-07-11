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

    // NUMPAD8: two-step equip-by-def test of the carried-weapon chain.
    // Press 1: factory-create a broadsword and post the pickup action (the
    // game wires it as a proper inventory weapon; acquire popup expected).
    // Press 2: assign it to the carried-melee holder (+0x134) and call
    // RegenerateCarriedWeapons — success = the broadsword appears on the
    // hero's back. This is the puppet weapon-visual apply path.
    void WeaponEquipProbe(CThingPlayerCreature* creature);
}
