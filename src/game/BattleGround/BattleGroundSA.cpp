/*
 * This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "Entities/Player.h"
#include "Entities/GameObject.h"
#include "Entities/Vehicle.h"
#include "BattleGround.h"
#include "BattleGroundSA.h"
#include "Tools/Language.h"
#include "Globals/ObjectMgr.h"

BattleGroundSA::BattleGroundSA(): m_defendingTeamIdx(TEAM_INDEX_NEUTRAL), m_battleRoundTimer(0), m_boatStartTimer(0), m_battleStage(BG_SA_STAGE_ROUND_1)
{
    // set battleground start message ids
    m_startMessageIds[BG_STARTING_EVENT_FIRST]  = LANG_BG_SA_START_TWO_MINUTES;
    m_startMessageIds[BG_STARTING_EVENT_SECOND] = LANG_BG_SA_START_ONE_MINUTE;
    m_startMessageIds[BG_STARTING_EVENT_THIRD]  = LANG_BG_SA_START_HALF_MINUTE;
    m_startMessageIds[BG_STARTING_EVENT_FOURTH] = 0;
}

void BattleGroundSA::Reset()
{
    // call parent's class reset
    BattleGround::Reset();

    m_defendingTeamIdx = PvpTeamIndex(urand(0, 1));

    m_boatStartTimer = BG_SA_TIMER_BOAT_START;
    m_battleRoundTimer = BG_SA_TIMER_ROUND_LENGTH;

    for (uint8 i = 0; i < PVP_TEAM_COUNT; ++i)
    {
        m_scoreCount[i] = 0;
        m_winTime[i] = 0;
    }

    // setup the battleground
    SetupBattleground();
}

void BattleGroundSA::Update(uint32 diff)
{
    BattleGround::Update(diff);

    if (m_boatStartTimer)
    {
        if (m_boatStartTimer <= diff)
        {
            // Set the boats in motion
            for (const auto& guid : m_transportShipGuids[GetAttacker()])
            {
                if (GameObject* pShip = GetBgMap()->GetGameObject(guid))
                {
                    pShip->SetRespawnTime(30);
                    pShip->Refresh();
                    pShip->SetUInt32Value(GAMEOBJECT_LEVEL, 0);
                    pShip->SetGoState(GO_STATE_READY);
                    pShip->SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_IN_USE);
                }
            }

            // ToDo: stop the boats when they reach the destination. Should be handled by event id 22095 and 18829
            m_boatStartTimer = 0;
        }
        else
            m_boatStartTimer -= diff;
    }

    if (GetStatus() == STATUS_IN_PROGRESS)
    {
        // battle timer
        if (m_battleRoundTimer)
        {
            if (m_battleRoundTimer < diff)
            {
                switch (m_battleStage)
                {
                    case BG_SA_STAGE_ROUND_1:
                        // eject all vehicle passengers and despawn vehicles immediately
                        for (const auto& guid : m_demolishersGuids)
                        {
                            if (Creature* demolisher = GetBgMap()->GetCreature(guid))
                            {
                                demolisher->RemoveSpellsCausingAura(SPELL_AURA_CONTROL_VEHICLE);
                                demolisher->ForcedDespawn();
                            }
                        }
                        for (const auto& guid : m_tempDemolishersGuids)
                        {
                            if (Creature* demolisher = GetBgMap()->GetCreature(guid))
                            {
                                demolisher->RemoveSpellsCausingAura(SPELL_AURA_CONTROL_VEHICLE);
                                demolisher->ForcedDespawn();
                            }
                        }
                        for (const auto& guid : m_cannonsGuids)
                        {
                            if (Creature* cannon = GetBgMap()->GetCreature(guid))
                            {
                                cannon->RemoveSpellsCausingAura(SPELL_AURA_CONTROL_VEHICLE);
                                cannon->ForcedDespawn();
                            }
                        }
                        // send warning
                        SendBattlegroundWarning(LANG_BG_SA_ROUND_FINISHED);
                        UpdateWorldState(BG_SA_STATE_ENABLE_TIMER, 0);

                        // cast end of round spell
                        CastSpellOnTeam(BG_SA_SPELL_END_OF_ROUND, ALLIANCE);
                        CastSpellOnTeam(BG_SA_SPELL_END_OF_ROUND, HORDE);
                        m_battleRoundTimer = 5000;
                        break;
                    case BG_SA_STAGE_RESET:
                    {
                        // invert the defender
                        m_defendingTeamIdx = m_defendingTeamIdx == TEAM_INDEX_ALLIANCE ? TEAM_INDEX_HORDE : TEAM_INDEX_ALLIANCE;

                        // reset
                        SetupBattleground();

                        // setup player spells
                        for (auto& m_player : m_players)
                        {
                            if (!m_player.first)
                                continue;

                            Player* player = sObjectMgr.GetPlayer(m_player.first);
                            if (player)
                            {
                                // remove auras
                                player->RemoveAurasDueToSpell(BG_SA_SPELL_END_OF_ROUND);

                                // teleport to the right spot and set phase
                                TeleportPlayerToStartArea(player);
                            }
                        }

                        m_battleRoundTimer = 2000;
                        m_boatStartTimer = 2000;
                        break;
                    }
                    case BG_SA_STAGE_SECOND_ROUND_1:
                        SendMessageToAll(LANG_BG_SA_ROUND_START_ONE_MINUTE, CHAT_MSG_BG_SYSTEM_NEUTRAL);

                        // cast preparation again before the 2nd round
                        CastSpellOnTeam(SPELL_PREPARATION, ALLIANCE);
                        CastSpellOnTeam(SPELL_PREPARATION, HORDE);

                        // make sure that the cannons have the right faction set; for some reason this isn't always correctly set
                        for (const auto& guid : m_cannonsGuids)
                        {
                            if (Creature* cannon = GetBgMap()->GetCreature(guid))
                                cannon->SetFactionTemporary(sotaTeamFactions[m_defendingTeamIdx], TEMPFACTION_NONE);
                        }

                        m_battleRoundTimer = 30000;
                        break;
                    case BG_SA_STAGE_SECOND_ROUND_2:
                        SendMessageToAll(LANG_BG_SA_ROUND_START_HALF_MINUTE, CHAT_MSG_BG_SYSTEM_NEUTRAL);
                        m_battleRoundTimer = 30000;
                        break;
                    case BG_SA_STAGE_SECOND_ROUND_3:

                        // remove preparation aura
                        for (auto& m_player : m_players)
                            if (Player* player = sObjectMgr.GetPlayer(m_player.first))
                                player->RemoveAurasDueToSpell(SPELL_PREPARATION);

                        SendBattlegroundWarning(LANG_BG_SA_BEGIN);
                        UpdateWorldState(BG_SA_STATE_ENABLE_TIMER, 1);
                        EnableDemolishers();
                        StartTimedAchievement(ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET, GetAttacker() == TEAM_INDEX_ALLIANCE ? BG_SA_ACHIEV_START_ID_STORM_BEACH_ALLY : BG_SA_ACHIEV_START_ID_STORM_BEACH_HORDE);
                        m_battleRoundTimer = BG_SA_TIMER_ROUND_LENGTH;
                        break;
                    case BG_SA_STAGE_ROUND_2:
                        ProcessBattlegroundWinner();
                        m_battleRoundTimer = 0;
                        break;
                }

                ++m_battleStage;
            }
            else
            {
                // update timer - if BattlegroundMgr update timer interval needs to be lowered replace this line with the commented-out ones below
                if (m_battleStage == BG_SA_STAGE_ROUND_1 || m_battleStage == BG_SA_STAGE_ROUND_2)
                    UpdateTimerWorldState();

                /*if (m_zoneUpdateTimer < diff)
                {
                    // update timer
                    UpdateTimerWorldState();
                    m_zoneUpdateTimer = BG_SA_TIMER_UPDATE_TIME;
                }
                else
                    m_zoneUpdateTimer -= diff;*/

                m_battleRoundTimer -= diff;
            }
        }
    }
}

void BattleGroundSA::AddPlayer(Player* player)
{
    BattleGround::AddPlayer(player);
    // create score and add it to map, default values are set in constructor
    BattleGroundSAScore* score = new BattleGroundSAScore;

    m_playerScores[player->GetObjectGuid()] = score;

    TeleportPlayerToStartArea(player);
}

// function to teleport player to the starting area
void BattleGroundSA::TeleportPlayerToStartArea(Player* player)
{
    // Teleport player to the correct location
    if (GetTeamIndexByTeamId(player->GetTeam()) == GetAttacker())
    {
        // TODO: make this location more dyanmic, depending on the transport real time position
        player->CastSpell(player, BG_SA_SPELL_TELEPORT_ATTACKERS, TRIGGERED_OLD_TRIGGERED);

        // Note: the following code is temporary until spell effect 60178 is implemented
        uint8 randLoc = (urand(0, 1));

        // in case the battle is already in progress use the dock locations
        if (GetStatus() == STATUS_IN_PROGRESS && (m_battleStage == BG_SA_STAGE_ROUND_2 || m_battleStage == BG_SA_STAGE_ROUND_1))
            randLoc = (urand(2, 3));

        player->TeleportTo(player->GetMapId(), strandTeleportLoc[randLoc][0], strandTeleportLoc[randLoc][1], strandTeleportLoc[randLoc][2], strandTeleportLoc[randLoc][3]);
    }
    else
        player->CastSpell(player, BG_SA_SPELL_TELEPORT_DEFENDER, TRIGGERED_OLD_TRIGGERED);
}

void BattleGroundSA::StartingEventOpenDoors()
{
    EnableDemolishers();
    SetupGraveyards();

    SendBattlegroundWarning(LANG_BG_SA_BEGIN);
    UpdateWorldState(BG_SA_STATE_ENABLE_TIMER, 1);
    StartTimedAchievement(ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET, GetAttacker() == TEAM_INDEX_ALLIANCE ? BG_SA_ACHIEV_START_ID_STORM_BEACH_ALLY : BG_SA_ACHIEV_START_ID_STORM_BEACH_HORDE);
}

// function to allow demolishers to be used by players
void BattleGroundSA::EnableDemolishers()
{
    for (const auto& guid : m_demolishersGuids)
    {
        if (Creature* demolisher = GetBgMap()->GetCreature(guid))
            demolisher->SetFactionTemporary(sotaTeamFactions[GetAttacker()], TEMPFACTION_TOGGLE_NOT_SELECTABLE);
    }

    m_noScratchAchiev = true;
    m_defenseAncients = true;
}

void BattleGroundSA::SetupGraveyards()
{
    Creature* master = GetBgMap()->GetCreature(m_battlegroundMasterGuid);
    if (!master)
        return;

    uint32 defenderHealerEntry = GetAttacker() == TEAM_INDEX_ALLIANCE ? NPC_SPIRIT_GUIDE_H : NPC_SPIRIT_GUIDE_A;

    // summon the spirit healer
    for (uint8 i = 0; i < BG_SA_MAX_GRAVEYARDS; ++i)
        if (Creature* healer = master->SummonCreature(defenderHealerEntry, strandGraveyardData[i].x, strandGraveyardData[i].y, strandGraveyardData[i].z, strandGraveyardData[i].o, TEMPSPAWN_DEAD_DESPAWN, 0))
            m_spiritHealersGuid[i] = healer->GetObjectGuid();
};

void BattleGroundSA::UpdatePlayerScore(Player* source, uint32 type, uint32 value)
{
    BattleGroundScoreMap::iterator itr = m_playerScores.find(source->GetObjectGuid());
    if (itr == m_playerScores.end())
        return;

    switch (type)
    {
        case SCORE_DEMOLISHERS_DESTROYED:
            ((BattleGroundSAScore*)itr->second)->demolishersDestroyed += value;
            break;
        case SCORE_GATES_DESTROYED:
            ((BattleGroundSAScore*)itr->second)->gatesDestroyed += value;
            break;
        default:
            BattleGround::UpdatePlayerScore(source, type, value);
            break;
    }
}

void BattleGroundSA::HandleCreatureCreate(Creature* creature)
{
    switch (creature->GetEntry())
    {
        case BG_SA_VEHICLE_CANNON:
            m_cannonsGuids.push_back(creature->GetObjectGuid());
            break;
        case BG_SA_VEHICLE_DEMOLISHER:
            if (creature->IsTemporarySummon())
            {
                creature->SetFactionTemporary(sotaTeamFactions[GetAttacker()], TEMPFACTION_TOGGLE_NOT_SELECTABLE);
                m_tempDemolishersGuids.push_back(creature->GetObjectGuid());
            }
            else
                m_demolishersGuids.push_back(creature->GetObjectGuid());
            break;
        case BG_SA_NPC_KANRETHAD:
            m_battlegroundMasterGuid = creature->GetObjectGuid();
            break;
        case BG_SA_NPC_INVISIBLE_STALKER:
            if (creature->GetPositionZ() > 80.0f)
                m_defenderTeleportStalkerGuid = creature->GetObjectGuid();
            else
                m_attackerTeleportStalkersGuids.push_back(creature->GetObjectGuid());
            break;
        case BG_SA_NPC_WORLD_TRIGGER:
            m_triggerGuids.push_back(creature->GetObjectGuid());
            break;
        case BG_SA_NPC_RIGGER_SPARKLIGHT:
            m_riggerGuid = creature->GetObjectGuid();
            break;
        case BG_SA_NPC_GORGRIL_RIGSPARK:
            m_gorgrilGuid = creature->GetObjectGuid();
            break;
    }
}

void BattleGroundSA::HandleGameObjectCreate(GameObject* go)
{
    switch (go->GetEntry())
    {
        case BG_SA_GO_TRANSPORT_SHIP_HORDE_1:
        case BG_SA_GO_TRANSPORT_SHIP_HORDE_2:
                m_transportShipGuids[TEAM_INDEX_HORDE].push_back(go->GetObjectGuid());
            break;
        case BG_SA_GO_TRANSPORT_SHIP_ALLIANCE_1:
        case BG_SA_GO_TRANSPORT_SHIP_ALLIANCE_2:
                m_transportShipGuids[TEAM_INDEX_ALLIANCE].push_back(go->GetObjectGuid());
            break;
        case BG_SA_GO_GATE_GREEN_EMERALD:
        case BG_SA_GO_GATE_PURPLE_AMETHYST:
        case BG_SA_GO_GATE_BLUE_SAPHIRE:
        case BG_SA_GO_GATE_RED_SUN:
        case BG_SA_GO_GATE_YELLOW_MOON:
        case BG_SA_GO_GATE_ANCIENT_SHRINE:
            m_gatesGuids.push_back(go->GetObjectGuid());
            break;
        case BG_SA_GO_TITAN_RELIC_ALLIANCE:
            m_relicGuid[TEAM_INDEX_ALLIANCE] = go->GetObjectGuid();
            break;
        case BG_SA_GO_TITAN_RELIC_HORDE:
            m_relicGuid[TEAM_INDEX_HORDE] = go->GetObjectGuid();
            break;
        case BG_SA_GO_GY_FLAG_ALLIANCE_EAST:
        case BG_SA_GO_GY_FLAG_ALLIANCE_WEST:
        case BG_SA_GO_GY_FLAG_ALLIANCE_SOUTH:
            m_graveyardBannersGuids[TEAM_INDEX_ALLIANCE].push_back(go->GetObjectGuid());
            break;
        case BG_SA_GO_GY_FLAG_HORDE_EAST:
        case BG_SA_GO_GY_FLAG_HORDE_WEST:
        case BG_SA_GO_GY_FLAG_HORDE_SOUTH:
            m_graveyardBannersGuids[TEAM_INDEX_HORDE].push_back(go->GetObjectGuid());
            break;
        case BG_SA_GO_SIGIL_YELLOW_MOON:
        case BG_SA_GO_SIGIL_GREEN_MOON:
        case BG_SA_GO_SIGIL_BLUE_MOON:
        case BG_SA_GO_SIGIL_RED_MOON:
        case BG_SA_GO_SIGIL_PURPLE_MOON:
            m_sigilsGuids.push_back(go->GetObjectGuid());
            break;
    }
}

// Function used to update the timer
void BattleGroundSA::UpdateTimerWorldState()
{
    // Calculate time
    uint32 secondsLeft = m_battleRoundTimer;

    UpdateWorldState(BG_SA_STATE_TIMER_SEC_FIRST_DIGIT, ((secondsLeft % 60000) % 10000) / 1000);
    UpdateWorldState(BG_SA_STATE_TIMER_SEC_SECOND_DIGIT, (secondsLeft % 60000) / 10000);
    UpdateWorldState(BG_SA_STATE_TIMER_MINUTES, secondsLeft / 60000);
    UpdateWorldState(BG_SA_STATE_ENABLE_TIMER, 1);
}

void BattleGroundSA::FillInitialWorldStates(WorldPacket& data, uint32& count)
{
    // fill attacker & defender states
    FillInitialWorldState(data, count, BG_SA_STATE_ATTACKER_ALLIANCE, GetAttacker() == TEAM_INDEX_ALLIANCE);
    FillInitialWorldState(data, count, BG_SA_STATE_ATTACKER_HORDE, GetAttacker() == TEAM_INDEX_HORDE);

    FillInitialWorldState(data, count, BG_SA_STATE_DEFENSE_TOKEN_HORDE, GetAttacker() == TEAM_INDEX_ALLIANCE);
    FillInitialWorldState(data, count, BG_SA_STATE_DEFENSE_TOKEN_ALLIANCE, GetAttacker() == TEAM_INDEX_HORDE);

    FillInitialWorldState(data, count, BG_SA_STATE_RIGHT_ATTACK_TOKEN_ALLIANCE, GetAttacker() == TEAM_INDEX_ALLIANCE);
    FillInitialWorldState(data, count, BG_SA_STATE_LEFT_ATTACK_TOKEN_ALLIANCE, GetAttacker() == TEAM_INDEX_ALLIANCE);
    FillInitialWorldState(data, count, BG_SA_STATE_RIGHT_ATTACK_TOKEN_HORDE, GetAttacker() == TEAM_INDEX_HORDE);
    FillInitialWorldState(data, count, BG_SA_STATE_LEFT_ATTACK_TOKEN_HORDE, GetAttacker() == TEAM_INDEX_HORDE);

    // fill gates states
    for (uint8 i = 0; i < BG_SA_MAX_GATES; ++i)
    {
        // special case for final gate: color depends on the defender (blue for alliance; red for horde)
        if (i == BG_SA_MAX_GATES - 1 && GetAttacker() == TEAM_INDEX_ALLIANCE)
            FillInitialWorldState(data, count, strandGates[i], m_gateStateValue[i] + 3);
        else
            FillInitialWorldState(data, count, strandGates[i], m_gateStateValue[i]);
    }

    // fill graveyard states
    for (uint8 i = 0; i < BG_SA_MAX_GRAVEYARDS; ++i)
    {
        FillInitialWorldState(data, count, sotaGraveyardData[i].worldStateAlliance, m_graveyardOwner[i] == TEAM_INDEX_ALLIANCE);
        FillInitialWorldState(data, count, sotaGraveyardData[i].worldStateHorde, m_graveyardOwner[i] == TEAM_INDEX_HORDE);
    }

    // fill timer states - will be updated later in the script
    FillInitialWorldState(data, count, BG_SA_STATE_ENABLE_TIMER, 0);
    FillInitialWorldState(data, count, BG_SA_STATE_TIMER_MINUTES, 0);
    FillInitialWorldState(data, count, BG_SA_STATE_TIMER_SEC_SECOND_DIGIT, 0);
    FillInitialWorldState(data, count, BG_SA_STATE_TIMER_SEC_FIRST_DIGIT, 0);
    FillInitialWorldState(data, count, BG_SA_STATE_BONUS_TIMER, 0);
}

// process the gate and relic events
bool BattleGroundSA::HandleEvent(uint32 eventId, GameObject* go, Unit* invoker)
{
    // handle round end by relic click
    if (eventId == BG_SA_EVENT_ID_RELIC)
    {
        DEBUG_LOG("BattleGroundSA: Relic with entry %u has been activated", go->GetEntry());

        // update score count and keep track of the time
        if (go->GetEntry() == BG_SA_GO_TITAN_RELIC_ALLIANCE)
        {
            ++m_scoreCount[TEAM_INDEX_ALLIANCE];
            m_winTime[TEAM_INDEX_ALLIANCE] = m_battleRoundTimer;
            CastSpellOnTeam(BG_SA_SPELL_ACHIEV_STORM_BEACH, ALLIANCE);
        }
        else if (go->GetEntry() == BG_SA_GO_TITAN_RELIC_HORDE)
        {
            ++m_scoreCount[TEAM_INDEX_HORDE];
            m_winTime[TEAM_INDEX_HORDE] = m_battleRoundTimer;
            CastSpellOnTeam(BG_SA_SPELL_ACHIEV_STORM_BEACH, HORDE);
        }

        // process winner
        if (m_battleStage == BG_SA_STAGE_ROUND_2)
            ProcessBattlegroundWinner();
        // prepare second round
        else if (m_battleStage == BG_SA_STAGE_ROUND_1)
            m_battleRoundTimer = 500;

        return false;
    }

    // special event for the ancient shrine door
    if (go->GetEntry() == BG_SA_GO_GATE_ANCIENT_SHRINE && eventId == BG_SA_EVENT_SHRINE_DOOR_DESTROY)
    {
        DEBUG_LOG("BattleGroundSA: Ancient Shrine id %u has been activated by event %u", go->GetEntry(), eventId);

        // Remove the no interact flag when door is destroyed
        if (GameObject* relic = go->GetMap()->GetGameObject(m_relicGuid[GetAttacker()]))
            relic->RemoveFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
    }

    // handle world state updates and warnings
    for (const auto& i : sotaObjectData)
    {
        if (go->GetEntry() == i.goEntry)
        {
            if (eventId == i.eventDamaged)
            {
                m_gateStateValue[i.index] = BG_SA_STATE_VALUE_GATE_DAMAGED;
                UpdateWorldState(i.worldState, m_gateStateValue[i.index]);
                SendBattlegroundWarning(i.messageDamaged);
                PlaySoundToAll(GetAttacker() == TEAM_INDEX_ALLIANCE ? BG_SA_SOUND_WALL_ATTACKED_ALLIANCE : BG_SA_SOUND_WALL_ATTACKED_HORDE);
            }
            else if (eventId == i.eventDestroyed)
            {
                m_gateStateValue[i.index] = BG_SA_STATE_VALUE_GATE_DESTROYED;
                UpdateWorldState(i.worldState, m_gateStateValue[i.index]);
                SendBattlegroundWarning(i.messagedDestroyed);
                PlaySoundToAll(GetAttacker() == TEAM_INDEX_ALLIANCE ? BG_SA_SOUND_WALL_DESTROYED_ALLIANCE : BG_SA_SOUND_WALL_DESTROYED_HORDE);

                if (invoker->GetTypeId() == TYPEID_PLAYER)
                    UpdatePlayerScore((Player*)invoker, SCORE_GATES_DESTROYED, 1);

                // fail the achievement
                m_defenseAncients = false;

                // despawn sigil
                if (strandSigils[i.index])
                    if (GameObject* sigil = GetClosestGameObjectWithEntry(go, strandSigils[i.index], 10.0f))
                        ChangeBgObjectSpawnState(sigil->GetObjectGuid(), RESPAWN_ONE_DAY);
            }
            else if (eventId == i.eventRebuild)
            {
                m_gateStateValue[i.index] = BG_SA_STATE_VALUE_GATE_INTACT;
                go->SetFaction(sotaTeamFactions[m_defendingTeamIdx]);
                UpdateWorldState(i.worldState, m_gateStateValue[i.index]);
            }

            break;
        }
    }

    return false;
}

// Called when a player clicks a graveyard banner
void BattleGroundSA::HandlePlayerClickedOnFlag(Player* player, GameObject* go)
{
    for (uint8 i = 0; i < BG_SA_MAX_GRAVEYARDS; ++i)
    {
        if (go->GetEntry() == sotaGraveyardData[i].goEntryAlliance || go->GetEntry() == sotaGraveyardData[i].goEntryHorde)
        {
            DEBUG_LOG("BattleGroundSA: Graveyard banner with id %u was clicked.", go->GetEntry());

            m_graveyardOwner[i] = GetAttacker();

            go->SetLootState(GO_JUST_DEACTIVATED);
            sObjectMgr.SetGraveYardLinkTeam(sotaGraveyardData[i].graveyardId, BG_SA_ZONE_ID_STRAND, GetTeamIdByTeamIndex(GetAttacker()));

            // spawn demolishers
            if (sotaGraveyardData[i].graveyardId == BG_SA_GRAVEYARD_ID_EAST)
            {
                for (uint8 i = 0; i < 3; ++i)
                    go->SummonCreature(sotaEastSpawns[i].entry, sotaEastSpawns[i].x, sotaEastSpawns[i].y, sotaEastSpawns[i].z, sotaEastSpawns[i].o, TEMPSPAWN_TIMED_OR_CORPSE_DESPAWN, 10 * MINUTE * IN_MILLISECONDS);
            }
            else if (sotaGraveyardData[i].graveyardId == BG_SA_GRAVEYARD_ID_WEST)
            {
                for (uint8 i = 0; i < 3; ++i)
                    go->SummonCreature(sotaWestSpawns[i].entry, sotaWestSpawns[i].x, sotaWestSpawns[i].y, sotaWestSpawns[i].z, sotaWestSpawns[i].o, TEMPSPAWN_TIMED_OR_CORPSE_DESPAWN, 10 * MINUTE * IN_MILLISECONDS);
            }

            // respawn the closest banner to the one that was clicked
            for (const auto& guid : m_graveyardBannersGuids[GetAttacker()])
            {
                if (GameObject* banner = GetBgMap()->GetGameObject(guid))
                {
                    if (banner->IsWithinDistInMap(go, 10.0f))
                    {
                        banner->SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
                        banner->SetRespawnTime(13 * MINUTE);
                        banner->Refresh();

                        break;
                    }
                }
            }

            // send text and update world states
            if (go->GetEntry() == sotaGraveyardData[i].goEntryHorde)
            {
                UpdateWorldState(sotaGraveyardData[i].worldStateHorde, 0);
                UpdateWorldState(sotaGraveyardData[i].worldStateAlliance, 1);

                SendBattlegroundWarning(sotaGraveyardData[i].textCaptureAlliance);
                PlaySoundToAll(BG_SA_SOUND_GRAVEYARD_TAKEN_ALLIANCE);
            }
            else if (go->GetEntry() == sotaGraveyardData[i].goEntryAlliance)
            {
                UpdateWorldState(sotaGraveyardData[i].worldStateAlliance, 0);
                UpdateWorldState(sotaGraveyardData[i].worldStateHorde, 1);

                SendBattlegroundWarning(sotaGraveyardData[i].textCaptureHorde);
                PlaySoundToAll(BG_SA_SOUND_GRAVEYARD_TAKEN_HORDE);
            }

            // update spirit healer; despawn old healer and summon new one
            if (Creature* pHealer = GetBgMap()->GetCreature(m_spiritHealersGuid[i]))
                pHealer->ForcedDespawn();

            uint32 healerEntry = go->GetEntry() == sotaGraveyardData[i].goEntryAlliance ? NPC_SPIRIT_GUIDE_H : NPC_SPIRIT_GUIDE_A;
            if (Creature* pHealer = go->SummonCreature(healerEntry, strandGraveyardData[i].x, strandGraveyardData[i].y, strandGraveyardData[i].z, strandGraveyardData[i].o, TEMPSPAWN_DEAD_DESPAWN, 0))
                m_spiritHealersGuid[i] = pHealer->GetObjectGuid();

            break;
        }
    }
}

void BattleGroundSA::HandleKillUnit(Creature* unit, Player* killer)
{
    if (!unit)
        return;

    if (unit->GetEntry() == BG_SA_VEHICLE_DEMOLISHER)
    {
        UpdatePlayerScore(killer, SCORE_DEMOLISHERS_DESTROYED, 1);
        m_noScratchAchiev = false;
    }
}

void BattleGroundSA::EndBattleGround(Team winner)
{
    // win reward
    if (winner == ALLIANCE)
        RewardHonorToTeam(GetBonusHonorFromKill(1), ALLIANCE);
    if (winner == HORDE)
        RewardHonorToTeam(GetBonusHonorFromKill(1), HORDE);

    // complete map_end rewards (even if no team wins)
    RewardHonorToTeam(GetBonusHonorFromKill(1), HORDE);
    RewardHonorToTeam(GetBonusHonorFromKill(1), ALLIANCE);

    BattleGround::EndBattleGround(winner);
}

// function that will process the battleground winner
void BattleGroundSA::ProcessBattlegroundWinner()
{
    Team winner = TEAM_NONE;

    // cast preparation again at the end of the rounds; required for achievement check
    CastSpellOnTeam(SPELL_PREPARATION, ALLIANCE);
    CastSpellOnTeam(SPELL_PREPARATION, HORDE);

    // in case of a tie verify the time counters
    if (m_scoreCount[TEAM_INDEX_ALLIANCE] == m_scoreCount[TEAM_INDEX_HORDE] && m_scoreCount[TEAM_INDEX_ALLIANCE] == 1)
    {
        // the higher the win time = the faster the team captured the relic
        if (m_winTime[TEAM_INDEX_ALLIANCE] > m_winTime[TEAM_INDEX_HORDE])
            winner = ALLIANCE;
        else if (m_winTime[TEAM_INDEX_ALLIANCE] < m_winTime[TEAM_INDEX_HORDE])
            winner = HORDE;
    }
    else if (m_scoreCount[TEAM_INDEX_ALLIANCE] > m_scoreCount[TEAM_INDEX_HORDE])
        winner = ALLIANCE;
    else if (m_scoreCount[TEAM_INDEX_ALLIANCE] < m_scoreCount[TEAM_INDEX_HORDE])
        winner = HORDE;

    DEBUG_LOG("BattleGroundSA: Winner team: %u", winner);

    // send game end sounds
    if (winner == ALLIANCE)
    {
        PlaySoundToTeam(BG_SA_SOUND_VICTORY_ALLIANCE, ALLIANCE);
        PlaySoundToTeam(BG_SA_SOUND_DEFEAT_HORDE, HORDE);
    }
    else if (winner == HORDE)
    {
        PlaySoundToTeam(BG_SA_SOUND_DEFEAT_ALLIANCE, ALLIANCE);
        PlaySoundToTeam(BG_SA_SOUND_VICTORY_HORDE, HORDE);
    }

    EndBattleGround(winner);
}

// Function to setup battleground
void BattleGroundSA::SetupBattleground()
{
    DEBUG_LOG("BattleGroundSA: Setup battleground for stage: %u", m_battleStage);

    // set initial gate state values
    for (unsigned int& i : m_gateStateValue)
        i = BG_SA_STATE_VALUE_GATE_INTACT;

    // reset cannons
    for (const auto& guid : m_cannonsGuids)
    {
        if (Creature* cannon = GetBgMap()->GetCreature(guid))
        {
            cannon->SetFactionTemporary(sotaTeamFactions[m_defendingTeamIdx], TEMPFACTION_NONE);

            if (!cannon->IsAlive())
                cannon->Respawn();
        }
    }

    // despawn goblins
    if (Creature* goblin = GetBgMap()->GetCreature(m_riggerGuid))
        goblin->ForcedDespawn();
    if (Creature* goblin = GetBgMap()->GetCreature(m_gorgrilGuid))
        goblin->ForcedDespawn();

    // reset demolishers; clean temp faction to re-enable the unit flag
    for (const auto& guid : m_demolishersGuids)
    {
        if (Creature* demolisher = GetBgMap()->GetCreature(guid))
        {
            if (!demolisher->IsAlive())
                demolisher->Respawn();

            demolisher->ClearTemporaryFaction();
        }
    }

    // despawn the spirit healers
    for (uint8 i = 0; i < BG_SA_MAX_GRAVEYARDS; ++i)
        if (Creature* pHealer = GetBgMap()->GetCreature(m_spiritHealersGuid[i]))
            pHealer->ForcedDespawn();

    // reset Gates
    if (Creature* master = GetBgMap()->GetCreature(m_battlegroundMasterGuid))
    {
        for (const auto& guid : m_gatesGuids)
        {
            if (GameObject* gate = GetBgMap()->GetGameObject(guid))
                gate->RebuildGameObject(master);
        }
    }

    // reset sigils
    for (const auto& guid : m_sigilsGuids)
        ChangeBgObjectSpawnState(guid, RESPAWN_IMMEDIATELY);

    // set capturable graveyard links and states
    for (uint8 i = 0; i < BG_SA_MAX_GRAVEYARDS; ++i)
    {
        m_graveyardOwner[i] = m_defendingTeamIdx;
        sObjectMgr.SetGraveYardLinkTeam(sotaGraveyardData[i].graveyardId, BG_SA_ZONE_ID_STRAND, GetTeamIdByTeamIndex(m_defendingTeamIdx));
    }

    // despawn the attacker banners
    for (const auto& guid : m_graveyardBannersGuids[GetAttacker()])
        ChangeBgObjectSpawnState(guid, RESPAWN_ONE_DAY);

    // respawn graveyard banners and remove the no interact flag
    for (const auto& guid : m_graveyardBannersGuids[m_defendingTeamIdx])
    {
        ChangeBgObjectSpawnState(guid, RESPAWN_IMMEDIATELY);

        if (GameObject* banner = GetBgMap()->GetGameObject(guid))
            banner->RemoveFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
    }

    // set static graveyards
    sObjectMgr.SetGraveYardLinkTeam(BG_SA_GRAVEYARD_ID_SHRINE, BG_SA_ZONE_ID_STRAND, GetTeamIdByTeamIndex(m_defendingTeamIdx));
    sObjectMgr.SetGraveYardLinkTeam(BG_SA_GRAVEYARD_ID_BEACH, BG_SA_ZONE_ID_STRAND, GetTeamIdByTeamIndex(GetAttacker()));
    sObjectMgr.SetGraveYardLinkTeam(BG_SA_GRAVEYARD_ID_SHIP, BG_SA_ZONE_ID_STRAND, GetTeamIdByTeamIndex(GetAttacker()));
}

// Function to handle the warnings
void BattleGroundSA::SendBattlegroundWarning(int32 messageId)
{
    // The warnings are sent by a random trigger; it's difficult to figure out the exact logic here
    ObjectGuid guid = m_triggerGuids[urand(0, m_triggerGuids.size() - 1)];

    SendMessageToAll(messageId, CHAT_MSG_RAID_BOSS_EMOTE, LANG_UNIVERSAL, guid);
}

void BattleGroundSA::HandleGameObjectDamaged(Player* player, GameObject* object, uint32 spellId)
{
    if (spellId == BG_SA_SPELL_SEAFORIUM_BLAST)
        player->CastSpell(player, BG_SA_SPELL_ACHIEV_SEAFORIUM_DAMAGE, TRIGGERED_OLD_TRIGGERED);
}

bool BattleGroundSA::CheckAchievementCriteriaMeet(uint32 criteria_id, Player const* source, Unit const* target, uint32 miscvalue1)
{
    switch (criteria_id)
    {
        case BG_SA_CRIT_NOT_A_SCRATCH_ALLY:
        case BG_SA_CRIT_NOT_A_SCRATCH_HORDE:
            return m_noScratchAchiev;
        case BG_SA_CRIT_DEFENSE_ANCIENTS_ALLY:
        case BG_SA_CRIT_DEFENSE_ANCIENTS_HORDE:
            return m_defenseAncients && GetTeamIndexByTeamId(source->GetTeam()) == m_defendingTeamIdx;
        case BG_SA_CRIT_ARTILLERY_EXPERT:
        case BG_SA_CRIT_ARTILLERY_VETERAN:
            return source->IsBoarded() && source->GetTransportInfo()->GetTransport()->GetEntry() == BG_SA_VEHICLE_CANNON;
        case BG_SA_CRIT_DROP_IT:
        case BG_SA_CRIT_DROP_IT_NOW:
            // check aura in script because the DB condition uses a different function which returns wrong result when player is killed
            return target->HasAura(BG_SA_SPELL_CARRY_SEAFORIUM);
    }

    return false;
}

// Check condition for SotA phasing aura
bool BattleGroundSA::IsConditionFulfilled(Player const* source, uint32 conditionId, WorldObject const* conditionSource, uint32 conditionSourceType)
{
    switch (conditionId)
    {
        case BG_SA_COND_DEFENDER_ALLIANCE:
            return m_defendingTeamIdx == TEAM_INDEX_ALLIANCE;
        case BG_SA_COND_DEFENDER_HORDE:
            return m_defendingTeamIdx == TEAM_INDEX_HORDE;
    }

    return false;
}
