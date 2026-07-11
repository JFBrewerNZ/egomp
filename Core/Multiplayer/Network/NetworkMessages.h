#pragma once

#include "SLikeNet/MessageIdentifiers.h"

// Bumped whenever a payload changes shape; mismatching peers are rejected
// at ID_CONNECTION_NOTIFICATION.
const int EGOMP_PROTOCOL_VERSION = 9;

// Every payload is prefixed with its [uint8 messageId].
// "host" below means the session authority: either a hosting player (P2P)
// or a dedicated server; both use networkId 0.
enum NetworkMessages
{
    // client -> host: [int protocolVersion]
    // Announces the connection once it is accepted.
    ID_CONNECTION_NOTIFICATION = ID_USER_PACKET_ENUM,

    // host -> client: [int networkId][uint8 hostIsPlayer][C3DVector position]
    // Assigns the client its networkId. hostIsPlayer=1: the host is a player,
    // teleport to `position` and announce after the region loads.
    // hostIsPlayer=0: dedicated server, stay put and announce immediately.
    ID_CREATE_LOCAL_NET_PLAYER,

    // client -> host (announce), host -> clients (relay):
    // [int networkId][int defGlobalIndex][C3DVector position][int regionIndex]
    ID_CREATE_NET_PLAYER,

    // host -> joining client: [int count], then count x
    // ([int networkId][int defGlobalIndex][C3DVector position][int regionIndex])
    ID_CREATE_NET_PLAYERS,

    // host -> clients: [int networkId]
    ID_DESTROY_NET_PLAYER,

    // sender -> host -> other clients:
    // [int networkId][C3DVector position][C3DVector movementAcceleration]
    ID_PLAYER_MOVEMENT,

    // sender -> host -> other clients:
    // [int networkId][C3DVector up][C3DVector forward]
    ID_PLAYER_ROTATION,

    // sender -> host -> other clients:
    // [int networkId][int regionIndex][C3DVector position]
    // Sent after the sender finishes loading into a region.
    ID_PLAYER_REGION,

    // sender -> host -> other clients:
    // [int networkId][9 x uint32 morph blob][12 x int expSpentOn]
    // [int meleeWeaponDef][int rangedWeaponDef]
    // [int count][count x int modifierDefGlobalIndex]
    // The sender's raw CTCHeroMorph body-shape block (+0x40..+0x63:
    // strength/will/skill/age/morality/fatness/tan/teenager), per-stat
    // spent experience, and full appearance-modifier set (hair/beard/
    // hat/horns...). Sent shortly after connecting and whenever any of it
    // changes; the host stores the latest per player and seeds it to
    // joining clients.
    ID_PLAYER_APPEARANCE,

    // sender -> host -> other clients:
    // [int networkId][int actionType][C3DVector direction]
    // A momentary combat action the sender's hero performed (roll, block,
    // draw, swing, ...) plus its movement direction at that instant (for
    // directional moves like the roll). Reconstructed on each remote puppet
    // so it mirrors the move. Sent unreliably — a dropped action is simply
    // not mirrored.
    ID_PLAYER_ACTION
};
