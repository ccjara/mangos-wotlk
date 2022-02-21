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

#include "Spells/Scripts/SpellScript.h"
#include "Spells/SpellAuras.h"

struct spell_preparation : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        if (spell->GetCaster()->IsPlayer())
        {
            // immediately finishes the cooldown on certain Rogue abilities
            auto cdCheck = [](SpellEntry const & spellEntry) -> bool { return (spellEntry.SpellFamilyName == SPELLFAMILY_ROGUE && (spellEntry.SpellFamilyFlags & uint64(0x0000024000000860))); };
            static_cast<Player*>(spell->GetCaster())->RemoveSomeCooldown(cdCheck);
        }
    }
};

enum
{
    SPELL_DISTRACT      = 1725,
    SPELL_EARTHBIND     = 3600,
    SPELL_MASS_DISPEL   = 39897,
    SPELL_MASS_DISPEL_2 = 32592,
};

// Warning: Also currently used by Prowl
struct Stealth : public AuraScript
{
    bool OnCheckProc(Aura* /*aura*/, ProcExecutionData& data) const override // per 1.12.0 patch notes - no other indication of how it works
    {
        if (data.spell)
        {
            switch (data.spell->m_spellInfo->Id)
            {
                case SPELL_DISTRACT:
                case SPELL_EARTHBIND:
                case SPELL_MASS_DISPEL:
                case SPELL_MASS_DISPEL_2:
                    return false;
            }
        }
        return true;
    }
};

void CastHighestStealthRank(Unit* caster)
{
    if (!caster->IsPlayer())
        return;

    // get highest rank of the Stealth spell
    SpellEntry const* stealthSpellEntry = nullptr;
    const PlayerSpellMap& sp_list = static_cast<Player*>(caster)->GetSpellMap();
    for (const auto& itr : sp_list)
    {
        // only highest rank is shown in spell book, so simply check if shown in spell book
        if (!itr.second.active || itr.second.disabled || itr.second.state == PLAYERSPELL_REMOVED)
            continue;

        SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(itr.first);
        if (!spellInfo)
            continue;

        if (spellInfo->IsFitToFamily(SPELLFAMILY_ROGUE, uint64(0x0000000000400000)))
        {
            stealthSpellEntry = spellInfo;
            break;
        }
    }

    // no Stealth spell found
    if (!stealthSpellEntry)
        return;

    // reset cooldown on it if needed
    if (!caster->IsSpellReady(*stealthSpellEntry))
        caster->RemoveSpellCooldown(*stealthSpellEntry);

    caster->CastSpell(nullptr, stealthSpellEntry, TRIGGERED_OLD_TRIGGERED);
}

struct VanishRogue : public SpellScript
{
    void OnCast(Spell* spell) const override
    {
        CastHighestStealthRank(spell->GetCaster());
    }
};

// 13983 - Setup
struct SetupRogue : public AuraScript
{
    bool OnCheckProc(Aura* /*aura*/, ProcExecutionData& data) const override
    {
        return data.victim->GetTarget() == data.attacker;
    }
};

struct KillingSpreeStorage : public ScriptStorage
{
    GuidSet targets;
};

// 51690 - Killing Spree
struct KillingSpree : public SpellScript, public AuraScript
{
    void OnAuraInit(Aura* aura) const override
    {
        // this is likely emulation due to us killing off spell after it ends even though auras still exist
        if (aura->GetEffIndex() == EFFECT_INDEX_0)
            aura->SetScriptStorage(new KillingSpreeStorage());
    }

    void OnApply(Aura* aura, bool apply) const override
    {
        if (!apply || aura->GetEffIndex() != EFFECT_INDEX_0)
            return;

        Unit* target = aura->GetTarget();
        target->CastSpell(nullptr, 61851, TRIGGERED_IGNORE_GCD | TRIGGERED_IGNORE_CURRENT_CASTED_SPELL | TRIGGERED_HIDE_CAST_IN_COMBAT_LOG);
    }

    void OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        if (effIdx != EFFECT_INDEX_1 || !spell->GetUnitTarget())
            return;
        // aura is self effect so this must be executed after in all cases
        if (Aura* aura = spell->GetCaster()->GetAura(spell->m_spellInfo->Id, EFFECT_INDEX_0))
            if (KillingSpreeStorage* storage = dynamic_cast<KillingSpreeStorage*>(aura->GetScriptStorage()))
                storage->targets.insert(spell->GetUnitTarget()->GetObjectGuid());
    }

    void OnPeriodicDummy(Aura* aura) const override
    {
        if (KillingSpreeStorage* storage = dynamic_cast<KillingSpreeStorage*>(aura->GetScriptStorage()))
        {
            Unit* target = aura->GetTarget();
            Unit* victim = nullptr;
            std::vector<Unit*> eligibleUnits;
            for (ObjectGuid guid : storage->targets)
                if (Unit* unit = target->GetMap()->GetUnit(guid))
                    if (unit->IsAlive() && target->CanAttackSpell(unit) && target->IsWithinCombatDistInMap(unit, 10.f))
                        eligibleUnits.push_back(unit);

            if (eligibleUnits.size() > 0)
                victim = eligibleUnits[urand(0, eligibleUnits.size() - 1)];

            if (victim)
            {
                target->CastSpell(victim, 57840, TRIGGERED_IGNORE_GCD | TRIGGERED_IGNORE_CURRENT_CASTED_SPELL | TRIGGERED_HIDE_CAST_IN_COMBAT_LOG);
                target->CastSpell(victim, 57841, TRIGGERED_IGNORE_GCD | TRIGGERED_IGNORE_CURRENT_CASTED_SPELL | TRIGGERED_HIDE_CAST_IN_COMBAT_LOG);
            }
        }
    }
};

void LoadRogueScripts()
{
    RegisterSpellScript<spell_preparation>("spell_preparation");
    RegisterSpellScript<Stealth>("spell_stealth");
    RegisterSpellScript<VanishRogue>("spell_vanish");
    RegisterSpellScript<SetupRogue>("spell_setup_rogue");
    RegisterSpellScript<KillingSpree>("spell_killing_spree");
}