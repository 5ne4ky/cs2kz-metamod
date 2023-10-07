
#include "utils.h"
#include "convar.h"
#include "strtools.h"
#include "tier0/dbg.h"
#include "interfaces/interfaces.h"

#include "common.h"
#include "module.h"
#include "detours.h"
#include "virtual.h"
#include "tier0/memdbgon.h"
#include "recipientfilters.h"

#define FCVAR_FLAGS_TO_REMOVE (FCVAR_HIDDEN | FCVAR_DEVELOPMENTONLY | FCVAR_MISSING0 | FCVAR_MISSING1 | FCVAR_MISSING2 | FCVAR_MISSING3)

ClientPrintFilter_t *UTIL_ClientPrintFilter = NULL;

void modules::Initialize()
{
	modules::engine = new CModule(ROOTBIN, "engine2");
	modules::tier0 = new CModule(ROOTBIN, "tier0");
	modules::server = new CModule(GAMEBIN, "server");
}

bool interfaces::Initialize(ISmmAPI *ismm, char *error, size_t maxlen)
{
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, interfaces::pGameResourceServiceServer, CGameResourceService, GAMERESOURCESERVICESERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetServerFactory, g_pSource2GameClients, ISource2GameClients, INTERFACEVERSION_SERVERGAMECLIENTS);
	GET_V_IFACE_CURRENT(GetServerFactory, g_pSource2GameEntities, ISource2GameEntities, SOURCE2GAMEENTITIES_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, interfaces::pEngine, IVEngineServer2, INTERFACEVERSION_VENGINESERVER);
	GET_V_IFACE_CURRENT(GetServerFactory, interfaces::pServer, ISource2Server, INTERFACEVERSION_SERVERGAMEDLL);
	return true;
}

bool utils::Initialize(ISmmAPI *ismm, char *error, size_t maxlen)
{
	modules::Initialize();
	if (!interfaces::Initialize(ismm, error, maxlen))
	{
		return false;
	}

	utils::UnlockConVars();
	utils::UnlockConCommands();
	
	UTIL_ClientPrintFilter = (ClientPrintFilter_t *)modules::server->FindSignature((const byte *)sigs::UTIL_ClientPrintFilter.data, sigs::UTIL_ClientPrintFilter.length);

	InitDetours();
	return true;
}

void utils::Cleanup()
{
	FlushAllDetours();
}

CGlobalVars *utils::GetServerGlobals()
{
	return interfaces::pEngine->GetServerGlobals();
}

void utils::UnlockConVars()
{
	if (!g_pCVar)
		return;

	ConVar *pCvar = nullptr;
	ConVarHandle hCvarHandle;
	hCvarHandle.Set(0);

	// Can't use FindFirst/Next here as it would skip cvars with certain flags, so just loop through the handles
	do
	{
		pCvar = g_pCVar->GetConVar(hCvarHandle);

		hCvarHandle.Set(hCvarHandle.Get() + 1);

		if (!pCvar || !(pCvar->flags & FCVAR_FLAGS_TO_REMOVE))
			continue;

		pCvar->flags &= ~FCVAR_FLAGS_TO_REMOVE;
	} while (pCvar);

}

void utils::UnlockConCommands()
{
	if (!g_pCVar)
		return;

	ConCommand *pConCommand = nullptr;
	ConCommand *pInvalidCommand = g_pCVar->GetCommand(ConCommandHandle());
	ConCommandHandle hConCommandHandle;
	hConCommandHandle.Set(0);

	do
	{
		pConCommand = g_pCVar->GetCommand(hConCommandHandle);

		hConCommandHandle.Set(hConCommandHandle.Get() + 1);

		if (!pConCommand || pConCommand == pInvalidCommand || !(pConCommand->GetFlags() & FCVAR_FLAGS_TO_REMOVE))
			continue;

		pConCommand->RemoveFlags(FCVAR_FLAGS_TO_REMOVE);
	} while (pConCommand && pConCommand != pInvalidCommand);
}

bool utils::IsEntityPawn(CBaseEntity *entity)
{
	return CALL_VIRTUAL(bool, offsets::IsEntityPawn, entity);
}

bool utils::IsEntityController(CBaseEntity *entity)
{
	return CALL_VIRTUAL(bool, offsets::IsEntityController, entity);
}

CBasePlayerController *utils::GetController(CBaseEntity *entity)
{
	CBasePlayerController *controller = nullptr;

	if (utils::IsEntityPawn(entity))
	{
		return static_cast<CBasePlayerPawn *>(entity)->m_hController.Get();
	}
	else if (utils::IsEntityController(entity))
	{
		return static_cast<CBasePlayerController*>(entity);
	}
	else
	{
		return nullptr;
	}
}

CBasePlayerController *utils::GetController(CPlayerSlot slot)
{
	return static_cast<CBasePlayerController*>(g_pEntitySystem->GetBaseEntity(CEntityIndex(slot.Get() + 1)));
}

CPlayerSlot utils::GetEntityPlayerSlot(CBaseEntity *entity)
{
	CBasePlayerController *controller = utils::GetController(entity);
	if (!controller)
	{
		return -1;
	}
	else return controller->m_pEntity->m_EHandle.GetEntryIndex() - 1;
}

#define FORMAT_STRING(buffer) \
	va_list args; \
	va_start(args, format); \
	char buffer[1024]; \
	vsnprintf(buffer, sizeof(buffer), format, args); \
	va_end(args);

void utils::PrintConsole(CBaseEntity *entity, char *format, ...)
{
	FORMAT_STRING(buffer);
	CSingleRecipientFilter filter(utils::GetEntityPlayerSlot(entity).Get());
	UTIL_ClientPrintFilter(filter, HUD_PRINTCONSOLE, buffer, "", "", "", "");
}

void utils::PrintChat(CBaseEntity *entity, char *format, ...)
{
	FORMAT_STRING(buffer);
	CSingleRecipientFilter filter(utils::GetEntityPlayerSlot(entity).Get());
	UTIL_ClientPrintFilter(filter, HUD_PRINTTALK, buffer, "", "", "", "");
}

void utils::PrintCentre(CBaseEntity *entity, char *format, ...)
{
	FORMAT_STRING(buffer);
	CSingleRecipientFilter filter(utils::GetEntityPlayerSlot(entity).Get());
	UTIL_ClientPrintFilter(filter, HUD_PRINTCENTER, buffer, "", "", "", "");
}

void utils::PrintAlert(CBaseEntity *entity, char *format, ...)
{
	FORMAT_STRING(buffer);
	CSingleRecipientFilter filter(utils::GetEntityPlayerSlot(entity).Get());
	UTIL_ClientPrintFilter(filter, HUD_PRINTALERT, buffer, "", "", "", "");
}

void utils::PrintConsoleAll(char *format, ...)
{
	FORMAT_STRING(buffer);
	CBroadcastRecipientFilter filter;
	UTIL_ClientPrintFilter(filter, HUD_PRINTCONSOLE, buffer, "", "", "", "");
}

void utils::PrintChatAll(char *format, ...)
{
	FORMAT_STRING(buffer);
	CBroadcastRecipientFilter filter;
	UTIL_ClientPrintFilter(filter, HUD_PRINTTALK, buffer, "", "", "", "");
}

void utils::PrintCentreAll(char *format, ...)
{
	FORMAT_STRING(buffer);
	CBroadcastRecipientFilter filter;
	UTIL_ClientPrintFilter(filter, HUD_PRINTCENTER, buffer, "", "", "", "");
}

void utils::PrintAlertAll(char *format, ...)
{
	FORMAT_STRING(buffer);
	CBroadcastRecipientFilter filter;
	UTIL_ClientPrintFilter(filter, HUD_PRINTALERT, buffer, "", "", "", "");
}