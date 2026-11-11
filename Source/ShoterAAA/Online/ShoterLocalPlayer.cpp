#include "Online/ShoterLocalPlayer.h"
#include "Online/ShoterGameInstance.h"

FString UShoterLocalPlayer::GetNickname() const
{
	if (const UShoterGameInstance* GameInstance = Cast<UShoterGameInstance>(GetGameInstance()))
	{
		if (!GameInstance->IsUsingSteam())
		{
			const FString& SettingsName = GameInstance->GetUserSettings().PlayerName;
			if (!SettingsName.IsEmpty())
			{
				return SettingsName.Left(20);
			}
		}
	}
	return Super::GetNickname();
}
