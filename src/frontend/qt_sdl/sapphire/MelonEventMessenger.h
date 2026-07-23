#ifndef MELONEVENTMESSENGER_H
#define MELONEVENTMESSENGER_H

#include <Platform.h>
#include "renderer/Renderer.h"

namespace MelonDSAndroid
{

class MelonEventMessenger
{
public:
    virtual void onRumbleStart(int durationMs) = 0;
    virtual void onRumbleStop() = 0;
    virtual void onEmulatorStop(melonDS::Platform::StopReason reason) = 0;
    virtual void onRendererInitFailed(Renderer renderer) = 0;
    virtual void onVulkanCompileProgress(int stageId, int current, int total) {}

    virtual void onAchievementPrimed(long achievementId) = 0;
    virtual void onAchievementTriggered(long achievementId) = 0;
    virtual void onAchievementUnprimed(long achievementId) = 0;
    virtual void onAchievementProgressUpdated(long achievementId, unsigned int current, unsigned int target, std::string progress) = 0;
    virtual void onAchievementProgressHidden(long achievementId) {}
    virtual void onLeaderboardAttemptStarted(long leaderboardId) = 0;
    virtual void onLeaderboardAttemptUpdated(long leaderboardId, std::string formattedValue) = 0;
    virtual void onLeaderboardAttemptCanceled(long leaderboardId) = 0;
    virtual void onLeaderboardTrackerHidden(long leaderboardId) {}
    virtual void onLeaderboardAttemptCompleted(long leaderboardId, int value, std::string formattedValue) = 0;
    virtual void onAchievementGameCompleted(long subsetId) = 0;
    virtual void onAchievementSubsetCompleted(long subsetId) = 0;
    virtual void onRetroAchievementsServerError(std::string api, long relatedId, int result, std::string message) = 0;
    virtual void onRetroAchievementsDisconnected() = 0;
    virtual void onRetroAchievementsReconnected() = 0;
};

}

#endif // MELONEVENTMESSENGER_H
