#include "Core/ShoterTypes.h"

#define LOCTEXT_NAMESPACE "ShoterTypes"

FLinearColor ShoterTeam::GetColor(uint8 TeamId)
{
	switch (TeamId)
	{
	case Blue:	return FLinearColor(0.05f, 0.55f, 1.0f);
	case Red:	return FLinearColor(1.0f, 0.15f, 0.08f);
	default:	return FLinearColor(1.0f, 0.6f, 0.05f);
	}
}

FText ShoterTeam::GetDisplayName(uint8 TeamId)
{
	switch (TeamId)
	{
	case Blue:	return LOCTEXT("TeamBlue", "AZUL");
	case Red:	return LOCTEXT("TeamRed", "ROJO");
	default:	return FText::GetEmpty();
	}
}

FString ShoterMatch::GetGameAlias(EShoterMatchType Type)
{
	switch (Type)
	{
	case EShoterMatchType::FreeForAll:	return TEXT("FFA");
	case EShoterMatchType::Zombies:		return TEXT("Zombies");
	default:							return TEXT("TDM");
	}
}

FText ShoterMatch::GetDisplayName(EShoterMatchType Type)
{
	switch (Type)
	{
	case EShoterMatchType::FreeForAll:	return LOCTEXT("ModeFFA", "Todos contra todos");
	case EShoterMatchType::Zombies:		return LOCTEXT("ModeZombies", "Zombies (cooperativo)");
	default:							return LOCTEXT("ModeTDM", "Duelo por equipos");
	}
}

EShoterMatchType ShoterMatch::FromAlias(const FString& Alias)
{
	if (Alias.Equals(TEXT("FFA"), ESearchCase::IgnoreCase))
	{
		return EShoterMatchType::FreeForAll;
	}
	if (Alias.Equals(TEXT("Zombies"), ESearchCase::IgnoreCase))
	{
		return EShoterMatchType::Zombies;
	}
	return EShoterMatchType::TeamDeathmatch;
}

#undef LOCTEXT_NAMESPACE
