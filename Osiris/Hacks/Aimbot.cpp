#define _USE_MATH_DEFINES
#include <cmath>

#include "Aimbot.h"
#include "../Config.h"
#include "../Interfaces.h"
#include "../Memory.h"
#include "../SDK/Entity.h"
#include "../SDK/UserCmd.h"
#include "../SDK/Vector.h"
#include "../SDK/WeaponId.h"

static constexpr int getBoneId(int weaponIndex) noexcept
{
    return 8 - config.aimbot.weapons[weaponIndex].bone;
}

static Vector calculateRelativeAngle(const Vector& source, const Vector& destination, const Vector& viewAngles) noexcept
{
    Vector delta = destination - source;
    constexpr auto radiansToDegrees = [](float radians) noexcept { return radians * 180 / static_cast<float>(M_PI); };
    Vector angles{ radiansToDegrees(atan2f(-delta.z, std::hypotf(delta.x, delta.y))) - viewAngles.x,
                   radiansToDegrees(atan2f(delta.y, delta.x)) - viewAngles.y };
    angles.normalize();
    return angles;
}

static int findTarget(UserCmd* cmd, int weaponIndex) noexcept
{
    auto bestFov = config.aimbot.weapons[weaponIndex].fov;
    auto bestTarget = 0;
    auto localPlayer = interfaces.entityList->getEntity(interfaces.engine->getLocalPlayer());
    auto localPlayerEyeOrigin = localPlayer->getEyePosition();

    for (int i = 1; i <= interfaces.engine->getMaxClients(); i++) {
        auto entity = interfaces.entityList->getEntity(i);
        if (!entity || entity == localPlayer || entity->isDormant() || !entity->isAlive() || !entity->isEnemy() || entity->getProperty<bool>("m_bGunGameImmunity"))
            continue;

        auto bonePosition = entity->getBonePosition(getBoneId(weaponIndex));
        if (config.aimbot.weapons[weaponIndex].visibleOnly && !entity->isVisible(bonePosition))
            continue;

        auto angle = calculateRelativeAngle(localPlayerEyeOrigin, bonePosition, cmd->viewangles);
        auto fov = std::hypotf(angle.x, angle.y);
        if (fov < bestFov) {
            bestFov = fov;
            bestTarget = i;
        }
    }
    return bestTarget;
}

void Aimbot::run(UserCmd* cmd) noexcept
{
    const auto localPlayer = interfaces.entityList->getEntity(interfaces.engine->getLocalPlayer());

    const auto activeWeapon = interfaces.entityList->getEntityFromHandle(localPlayer->getProperty<int>("m_hActiveWeapon"));
    if (!activeWeapon || !activeWeapon->getProperty<int>("m_iClip1"))
        return;

    auto weaponIndex = getWeaponIndex(activeWeapon->getProperty<WeaponId>("m_iItemDefinitionIndex"));
    if (!weaponIndex)
        return;

    if (!config.aimbot.weapons[weaponIndex].enabled)
        weaponIndex = 0;

    if (config.aimbot.weapons[weaponIndex].enabled && (cmd->buttons & UserCmd::IN_ATTACK || config.aimbot.weapons[weaponIndex].autoShot)) {
        static bool wasInAttackLastTick{ false };
        if (auto target = findTarget(cmd, weaponIndex)) {
            if (config.aimbot.weapons[weaponIndex].autoShot) {
                cmd->buttons |= UserCmd::IN_ATTACK;
                    if (wasInAttackLastTick && (activeWeapon->getProperty<WeaponId>("m_iItemDefinitionIndex") == WeaponId::Revolver
                        && activeWeapon->getProperty<float>("m_flPostponeFireReadyTime") <= memory.globalVars->currenttime
                        || activeWeapon->getProperty<WeaponId>("m_iItemDefinitionIndex") != WeaponId::Revolver))
                        cmd->buttons &= ~UserCmd::IN_ATTACK;
            }

            if (cmd->buttons & UserCmd::IN_ATTACK) {
                auto entity = interfaces.entityList->getEntity(target);
                auto angle = calculateRelativeAngle(localPlayer->getEyePosition(), entity->getBonePosition(getBoneId(weaponIndex)), cmd->viewangles);
                angle /= config.aimbot.weapons[weaponIndex].smooth;
                cmd->viewangles += angle;
                if (!config.aimbot.weapons[weaponIndex].silent || config.aimbot.weapons[weaponIndex].smooth > 1.0f)
                    interfaces.engine->setViewAngles(cmd->viewangles);
            }
        }

        wasInAttackLastTick = cmd->buttons & UserCmd::IN_ATTACK;
        static auto weaponRecoilScale = interfaces.cvar->findVar("weapon_recoil_scale");
        auto aimPunch = localPlayer->getProperty<Vector>("m_aimPunchAngle");
        aimPunch.x *= config.aimbot.weapons[weaponIndex].recoilControlY;
        aimPunch.y *= config.aimbot.weapons[weaponIndex].recoilControlX;

        cmd->viewangles -= aimPunch * weaponRecoilScale->getFloat();
    }
}
